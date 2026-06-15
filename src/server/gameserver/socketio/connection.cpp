#include "server/gameserver/socketio/connection.hpp"

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <spdlog/spdlog.h>

#include "server/gameserver/engineio/eio_protocol.hpp"
#include "server/gameserver/socketio/server.hpp"
#include "server/gameserver/socketio/sio_protocol.hpp"
#include "server/gameserver/socketio/socket_facade.hpp"

namespace sbc::server::gameserver::socketio {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace eio = engineio;
using steady = std::chrono::steady_clock;
using namespace asio::experimental::awaitable_operators;

namespace {
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

Connection::Connection(std::string sid, asio::any_io_executor ex, SocketIoServer* hub,
                       std::string address)
    : sid_(std::move(sid)),
      address_(std::move(address)),
      strand_(asio::make_strand(ex)),
      hub_(hub),
      park_timer_(strand_),
      ping_timer_(strand_),
      msg_bucket_(static_cast<std::size_t>(hub ? hub->limits().message_rate_per_sec : 20), 0) {
    park_timer_.expires_at(steady::time_point::max());
}

void Connection::start() {
    auto self = shared_from_this();
    asio::co_spawn(
        strand_, [self]() -> asio::awaitable<void> { co_await self->heartbeat_loop(); },
        asio::detached);
}

void Connection::push_locked(std::string packet) {
    outbox_.push_back(std::move(packet));
    // Wake a parked long-poll. Setting the expiry to the past also makes a poll
    // that parks slightly later return immediately, closing the wakeup race.
    park_timer_.expires_at(steady::time_point::min());
}

void Connection::enqueue_eio(std::string packet) {
    asio::post(strand_, [self = shared_from_this(), packet = std::move(packet)]() mutable {
        if (self->closing_) return;
        self->push_locked(std::move(packet));
    });
}

void Connection::emit(std::string_view event, const nlohmann::json& data) {
    enqueue_eio(eio::message(socketio::encode_event(event, data)));
}

asio::awaitable<std::string> Connection::drain_for_poll() {
    co_await asio::dispatch(strand_, asio::use_awaitable);
    for (;;) {
        // Once upgraded to WebSocket, the WS writer owns the outbox; release any
        // lingering long-poll with a NOOP so the client stops polling.
        if (ws_mode_) co_return std::string(1, static_cast<char>(eio::PacketType::Noop));
        if (!outbox_.empty()) {
            std::vector<std::string> pkts(outbox_.begin(), outbox_.end());
            outbox_.clear();
            std::string body = eio::encode_payload(pkts);
            spdlog::info("[eio {}] drain_for_poll: returning {} packet(s), first type='{}' len={}",
                         sid_, pkts.size(), body.empty() ? '?' : body.front(), body.size());
            co_return body;
        }
        if (closing_) co_return std::string(1, static_cast<char>(eio::PacketType::Close));
        park_timer_.expires_at(steady::time_point::max());
        boost::system::error_code ec;
        co_await park_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }
}

asio::awaitable<void> Connection::ingest(std::string body) {
    co_await asio::dispatch(strand_, asio::use_awaitable);
    if (closing_) co_return;
    for (const auto& enc : eio::decode_payload(body)) {
        auto pkt = eio::parse_packet(enc);
        if (!pkt) continue;
        switch (pkt->type) {
            case eio::PacketType::Ping:
                // EIO v4: the server pings; a client ping is unusual but we echo a
                // pong with the same data to be safe.
                push_locked(eio::Packet{eio::PacketType::Pong, pkt->data}.encode());
                break;
            case eio::PacketType::Pong:
                spdlog::info("[eio {}] ingest(poll): received PONG (on_strand={})", sid_,
                             strand_.running_in_this_thread());
                awaiting_pong_ = false;
                break;
            case eio::PacketType::Message:
                handle_sio_payload(pkt->data);
                break;
            case eio::PacketType::Close:
                do_disconnect("transport close");
                co_return;
            default:
                break;
        }
        if (closing_) co_return;
    }
}

void Connection::handle_sio_payload(std::string_view payload) {
    if (payload.empty()) return;
    const char t = payload.front();
    if (t == static_cast<char>(PacketType::Connect)) {
        if (!sio_connected_) {
            sio_connected_ = true;
            socket_ = std::make_shared<Socket>(weak_from_this(), hub_);
            push_locked(eio::message(socketio::connect_packet(sid_)));
            if (hub_) hub_->on_connection(socket_, address_);
        }
        return;
    }
    if (t == static_cast<char>(PacketType::Disconnect)) {
        do_disconnect("client namespace disconnect");
        return;
    }
    auto msg = socketio::parse(payload);
    if (msg && msg->type == PacketType::Event) dispatch_event(msg->event, msg->data);
}

void Connection::dispatch_event(const std::string& event, const nlohmann::json& data) {
    // More than `limit` events within a second drops the client. The window size
    // tracks the live setting, so a settings change applies on the next event.
    int limit = hub_ ? hub_->limits().message_rate_per_sec : 20;
    if (limit < 1) limit = 1;
    while (static_cast<int>(msg_bucket_.size()) > limit) msg_bucket_.pop_front();
    while (static_cast<int>(msg_bucket_.size()) < limit) msg_bucket_.push_front(0);
    std::int64_t last = msg_bucket_.front();
    msg_bucket_.pop_front();
    std::int64_t now = now_ms();
    msg_bucket_.push_back(now);
    if (now - last <= 1000) {
        push_locked(eio::message(socketio::encode_event("ForceDisconnect", "ErrorRateLimited")));
        do_disconnect("rate limited");
        return;
    }
    try {
        if (any_handler_) any_handler_(event, data);
        auto it = handlers_.find(event);
        if (it != handlers_.end()) it->second(data);
    } catch (const std::exception& e) {
        spdlog::warn("gameserver: handler for '{}' threw: {}", event, e.what());
    }
}

void Connection::on(std::string event, EventHandler fn) {
    handlers_[std::move(event)] = std::move(fn);
}
void Connection::off(const std::string& event) {
    handlers_.erase(event);
}
void Connection::set_disconnect_handler(DisconnectHandler fn) {
    disconnect_handler_ = std::move(fn);
}
void Connection::set_any_handler(AnyHandler fn) {
    any_handler_ = std::move(fn);
}

void Connection::note_joined(const std::string& room) {
    rooms_.insert(room);
}
void Connection::note_left(const std::string& room) {
    rooms_.erase(room);
}

void Connection::disconnect() {
    asio::post(strand_, [self = shared_from_this()]() {
        if (self->closing_) return;
        self->push_locked(eio::message("1"));
        self->do_disconnect("server namespace disconnect");
    });
}

void Connection::do_disconnect(const std::string& reason) {
    if (closing_) return;
    spdlog::info("[eio {}] do_disconnect: reason='{}' (ws_mode={}, on_strand={}, tid={})", sid_,
                 reason, ws_mode_, strand_.running_in_this_thread(),
                 std::hash<std::thread::id>{}(std::this_thread::get_id()));
    auto self = shared_from_this();  // keep alive past hub removal
    closing_ = true;
    if (disconnect_handler_) {
        try {
            disconnect_handler_(reason);
        } catch (const std::exception& e) {
            spdlog::warn("gameserver: disconnect handler threw: {}", e.what());
        }
    }
    handlers_.clear();
    disconnect_handler_ = nullptr;
    any_handler_ = nullptr;
    socket_.reset();
    if (hub_) {
        for (const auto& room : rooms_) hub_->leave_room(room, sid_);
    }
    rooms_.clear();
    park_timer_.expires_at(steady::time_point::min());  // wake any parked poll
    ping_timer_.cancel();
    if (hub_) hub_->remove(sid_);
}

asio::awaitable<void> Connection::heartbeat_loop() {
    using namespace std::chrono;
    boost::system::error_code ec;
    while (!closing_) {
        // Read the live timing each cycle so a settings change applies on the
        // next heartbeat without restarting the connection.
        int interval = hub_ ? hub_->limits().ping_interval_ms : eio::kPingInterval;
        int timeout = hub_ ? hub_->limits().ping_timeout_ms : eio::kPingTimeout;
        ping_timer_.expires_after(milliseconds(interval));
        co_await ping_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (closing_) break;
        awaiting_pong_ = true;
        spdlog::info("[eio {}] heartbeat: sending PING (ws_mode={}, on_strand={}, tid={})", sid_,
                     ws_mode_, strand_.running_in_this_thread(),
                     std::hash<std::thread::id>{}(std::this_thread::get_id()));
        push_locked(std::string(1, static_cast<char>(eio::PacketType::Ping)));
        ping_timer_.expires_after(milliseconds(timeout));
        co_await ping_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (closing_) break;
        if (awaiting_pong_) {
            spdlog::warn(
                "[eio {}] heartbeat: PING TIMEOUT, no pong within {}ms -> disconnect "
                "(ws_mode={}, awaiting_pong={})",
                sid_, timeout, ws_mode_, awaiting_pong_);
            do_disconnect("ping timeout");
            break;
        }
        spdlog::info("[eio {}] heartbeat: pong OK, awaiting_pong cleared", sid_);
    }
}

asio::awaitable<void> Connection::run_websocket(websocket::stream<beast::tcp_stream> ws) {
    // Engine.IO probe handshake. The long-poll transport stays authoritative
    // until the client sends "5" (UPGRADE) over the WebSocket.
    beast::flat_buffer buf;
    bool upgraded = false;
    for (;;) {
        auto [ec, n] = co_await ws.async_read(buf, asio::as_tuple(asio::use_awaitable));
        (void)n;
        if (ec) co_return;  // probe failed -> stay on long-polling, no disconnect
        std::string frame = beast::buffers_to_string(buf.data());
        buf.consume(buf.size());
        if (frame == "2probe") {
            auto [wec, wn] = co_await ws.async_write(asio::buffer(std::string("3probe")),
                                                     asio::as_tuple(asio::use_awaitable));
            (void)wn;
            if (wec) co_return;
        } else if (frame == "5") {
            upgraded = true;
            break;
        }
        // Any other frame during probe is ignored.
    }
    if (!upgraded) co_return;

    // Flip to WebSocket mode on the strand, releasing any parked long-poll.
    co_await asio::dispatch(strand_, asio::use_awaitable);
    if (closing_) co_return;
    ws_mode_ = true;
    ws_stop_ = false;
    spdlog::info("[eio {}] websocket UPGRADE complete (on_strand={}, tid={})", sid_,
                 strand_.running_in_this_thread(),
                 std::hash<std::thread::id>{}(std::this_thread::get_id()));
    park_timer_.expires_at(steady::time_point::min());

    // The writer drains the outbox; the reader processes inbound frames. They run
    // concurrently (Beast allows one in-flight read + one write) and their
    // completions are serialized on the strand.
    co_await (ws_writer(ws) || ws_reader(ws));

    co_await asio::dispatch(strand_, asio::use_awaitable);
    do_disconnect("websocket closed");
}

asio::awaitable<void> Connection::ws_writer(websocket::stream<beast::tcp_stream>& ws) {
    for (;;) {
        if (ws_stop_) co_return;
        if (!outbox_.empty()) {
            std::string pkt = std::move(outbox_.front());
            outbox_.pop_front();
            spdlog::info("[eio {}] ws_writer: writing type='{}' len={} (on_strand={}, tid={})",
                         sid_, pkt.empty() ? '?' : pkt.front(), pkt.size(),
                         strand_.running_in_this_thread(),
                         std::hash<std::thread::id>{}(std::this_thread::get_id()));
            auto [ec, n] =
                co_await ws.async_write(asio::buffer(pkt), asio::as_tuple(asio::use_awaitable));
            (void)n;
            if (ec) {
                spdlog::warn("[eio {}] ws_writer: write FAILED ec={}", sid_, ec.message());
                ws_stop_ = true;
                co_return;
            }
            continue;
        }
        if (closing_) {
            // Flush a final Engine.IO CLOSE then stop.
            std::string close_pkt(1, static_cast<char>(eio::PacketType::Close));
            co_await ws.async_write(asio::buffer(close_pkt), asio::as_tuple(asio::use_awaitable));
            ws_stop_ = true;
            co_return;
        }
        park_timer_.expires_at(steady::time_point::max());
        boost::system::error_code ec;
        co_await park_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        // Woken by a new packet, by closing, or by a stop request — re-check above.
    }
}

asio::awaitable<void> Connection::ws_reader(websocket::stream<beast::tcp_stream>& ws) {
    beast::flat_buffer buf;
    for (;;) {
        auto [ec, n] = co_await ws.async_read(buf, asio::as_tuple(asio::use_awaitable));
        (void)n;
        if (ec) {
            spdlog::warn("[eio {}] ws_reader: read exited ec={} ({}) awaiting_pong={} closing={}",
                         sid_, ec.value(), ec.message(), awaiting_pong_, closing_);
            break;
        }
        std::string frame = beast::buffers_to_string(buf.data());
        buf.consume(buf.size());
        spdlog::info("[eio {}] ws_reader: frame type='{}' len={} (on_strand={})", sid_,
                     frame.empty() ? '?' : frame.front(), frame.size(),
                     strand_.running_in_this_thread());
        auto pkt = eio::parse_packet(frame);
        if (pkt) {
            switch (pkt->type) {
                case eio::PacketType::Ping:
                    push_locked(eio::Packet{eio::PacketType::Pong, pkt->data}.encode());
                    break;
                case eio::PacketType::Pong:
                    spdlog::info("[eio {}] ws_reader: received PONG (on_strand={}, tid={})", sid_,
                                 strand_.running_in_this_thread(),
                                 std::hash<std::thread::id>{}(std::this_thread::get_id()));
                    awaiting_pong_ = false;
                    break;
                case eio::PacketType::Message:
                    handle_sio_payload(pkt->data);
                    break;
                case eio::PacketType::Close:
                    ec = boost::asio::error::eof;
                    break;
                default:
                    break;
            }
        }
        if (ec || closing_) break;
    }
    // Signal and wake the writer so the parallel group can complete.
    ws_stop_ = true;
    park_timer_.expires_at(steady::time_point::min());
}

}  // namespace sbc::server::gameserver::socketio
