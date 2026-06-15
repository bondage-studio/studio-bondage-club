#include "server/rpc/connection.hpp"

#include <chrono>
#include <utility>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "server/rpc/auth.hpp"
#include "server/rpc/dispatcher.hpp"
#include "server/rpc/session.hpp"

namespace sbc::server::rpc {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using steady = std::chrono::steady_clock;
using json = nlohmann::ordered_json;

RpcConnection::RpcConnection(WsStream ws, Strand strand, const RpcDispatcher& dispatcher,
                             const RpcAuth& auth)
    : ws_(std::move(ws)),
      strand_(std::move(strand)),
      dispatcher_(dispatcher),
      auth_(auth),
      park_timer_(strand_) {}

asio::awaitable<void> RpcConnection::run() {
    using namespace asio::experimental::awaitable_operators;
    ws_.text(true);

    co_await (reader() || writer());

    // Reader/writer have both stopped. Tear down any live subscriptions and send a
    // best-effort close (4401 when the handshake was rejected, normal otherwise).
    stop_ = true;
    if (session_) session_->cancel_all();
    boost::system::error_code ec;
    co_await ws_.async_close(
        websocket::close_reason(static_cast<websocket::close_code>(close_code_), close_reason_),
        asio::redirect_error(asio::use_awaitable, ec));
}

asio::awaitable<void> RpcConnection::reader() {
    beast::flat_buffer buf;
    for (;;) {
        auto [ec, n] = co_await ws_.async_read(buf, asio::as_tuple(asio::use_awaitable));
        (void)n;
        if (ec) break;
        std::string frame = beast::buffers_to_string(buf.data());
        buf.consume(buf.size());
        co_await handle_message(std::move(frame));
        if (stop_) break;
    }
    // Signal and wake the writer so the parallel group completes.
    stop_ = true;
    park_timer_.expires_at((steady::time_point::min)());
}

asio::awaitable<void> RpcConnection::writer() {
    for (;;) {
        if (stop_) co_return;
        if (!outbox_.empty()) {
            std::string pkt = std::move(outbox_.front());
            outbox_.pop_front();
            auto [ec, n] =
                co_await ws_.async_write(asio::buffer(pkt), asio::as_tuple(asio::use_awaitable));
            (void)n;
            if (ec) {
                stop_ = true;
                co_return;
            }
            continue;
        }
        // Park until a new frame is enqueued or stop is requested.
        park_timer_.expires_at((steady::time_point::max)());
        boost::system::error_code ec;
        co_await park_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }
}

void RpcConnection::push(std::string frame) {
    outbox_.push_back(std::move(frame));
    park_timer_.expires_at((steady::time_point::min)());  // wake the parked writer
}

asio::awaitable<void> RpcConnection::handle_message(std::string text) {
    json msg = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (msg.is_discarded() || !msg.is_object()) co_return;
    const std::string t = msg.value("t", std::string{});

    // Until the hello handshake succeeds, the only accepted frame is a hello with
    // a valid capability token. Anything else closes the connection with 4401.
    if (!authed_) {
        if (t == "hello" && auth_.verify(msg.value("token", std::string{}))) {
            authed_ = true;
            // The session runs its co_spawned requests/tickers on this connection's
            // strand and enqueues every outbound frame into the writer's outbox.
            auto weak = weak_from_this();
            session_ =
                std::make_shared<RpcSession>(strand_, dispatcher_, [weak](std::string frame) {
                    if (auto self = weak.lock()) self->push(std::move(frame));
                });
            push(json{{"t", "welcome"}}.dump());
        } else {
            close_code_ = 4401;
            close_reason_ = "unauthorized";
            stop_ = true;
        }
        co_return;
    }

    // req/sub/unsub are handled by the transport-agnostic session. We are already
    // on the strand (the reader runs here), so call it directly.
    if (session_) session_->handle_frame(msg);
    co_return;
}

}  // namespace sbc::server::rpc
