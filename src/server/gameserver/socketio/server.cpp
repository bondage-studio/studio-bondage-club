#include "server/gameserver/socketio/server.hpp"

#include <chrono>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "common/http_util.hpp"
#include "net/stream.hpp"
#include "server/gameserver/engineio/eio_protocol.hpp"
#include "server/gameserver/socketio/connection.hpp"
#include "server/gameserver/socketio/socket_facade.hpp"

namespace sbc::server::gameserver::socketio {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace eio = engineio;

namespace {

// rebuild_request reconstructs a Beast request from the internal Request so the
// WebSocket handshake can be completed on the hijacked socket.
http::request<http::empty_body> rebuild_request(const Request& req) {
    http::request<http::empty_body> out;
    out.method(http::verb::get);
    out.target(req.target);
    out.version(11);
    for (const auto& e : req.headers.entries()) out.insert(e.first, e.second);
    return out;
}

// query_param extracts a raw (non-decoded) query parameter value. Engine.IO
// parameters (EIO, transport, sid) are plain ASCII, so no percent-decoding is
// needed.
std::string query_param(std::string_view query, std::string_view key) {
    std::size_t pos = 0;
    while (pos <= query.size()) {
        std::size_t amp = query.find('&', pos);
        std::string_view part =
            query.substr(pos, amp == std::string_view::npos ? std::string_view::npos : amp - pos);
        std::size_t eq = part.find('=');
        if (eq != std::string_view::npos && part.substr(0, eq) == key)
            return std::string(part.substr(eq + 1));
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return {};
}

asio::awaitable<void> respond(ResponseWriter& w, int status, std::string body) {
    HeaderMap h;
    h.set("Content-Type", "text/plain; charset=UTF-8");
    co_await w.write_full(status, std::move(h), std::move(body));
}

// Prefer the trusted X-Forwarded-For last hop; fall back to the peer address.
std::string client_address(const Request& req) {
    std::string xff = req.headers.get("X-Forwarded-For");
    if (!xff.empty()) {
        std::size_t comma = xff.find_last_of(',');
        std::string last = comma == std::string::npos ? xff : xff.substr(comma + 1);
        std::size_t a = last.find_first_not_of(" \t");
        std::size_t b = last.find_last_not_of(" \t");
        if (a != std::string::npos) return last.substr(a, b - a + 1);
    }
    return req.remote_address.empty() ? "unknown" : req.remote_address;
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

SocketIoServer::SocketIoServer(asio::any_io_executor ex) : ex_(std::move(ex)) {}

void SocketIoServer::set_limits(TransportLimits l) {
    std::lock_guard<std::mutex> lock(limits_mu_);
    limits_ = l;
}

TransportLimits SocketIoServer::limits() const {
    std::lock_guard<std::mutex> lock(limits_mu_);
    return limits_;
}

asio::awaitable<void> SocketIoServer::handle_request(Request& req, ResponseWriter& w) {
    std::string sid = query_param(req.raw_query, "sid");
    const bool is_upgrade = sbc::iequals(req.headers.get("Upgrade"), "websocket") &&
                            sbc::header_contains_token(req.headers.get("Connection"), "upgrade");
    if (is_upgrade) {
        auto conn = find(sid);
        if (!conn) {
            co_await respond(w, 400, R"({"code":1,"message":"Session ID unknown"})");
            co_return;
        }
        co_await handle_ws_upgrade(req, w, std::move(conn));
        co_return;
    }

    if (req.is_get()) {
        if (sid.empty()) {
            co_await handle_handshake(req, w);
            co_return;
        }
        auto conn = find(sid);
        if (!conn) {
            co_await respond(w, 400, R"({"code":1,"message":"Session ID unknown"})");
            co_return;
        }
        co_await handle_poll(req, w, std::move(conn));
        co_return;
    }
    if (req.method == "POST") {
        auto conn = find(sid);
        if (!conn) {
            co_await respond(w, 400, R"({"code":1,"message":"Session ID unknown"})");
            co_return;
        }
        co_await handle_post(req, w, std::move(conn));
        co_return;
    }
    co_await respond(w, 405, "method not allowed");
}

asio::awaitable<void> SocketIoServer::handle_handshake(Request& req, ResponseWriter& w) {
    (void)req;
    std::string sid = eio::generate_sid();
    auto conn = std::make_shared<Connection>(sid, ex_, this, client_address(req));
    {
        std::unique_lock lock(conn_mu_);
        by_sid_[sid] = conn;
    }
    conn->start();
    TransportLimits lim = limits();
    eio::HandshakeConfig cfg;
    cfg.sid = sid;
    cfg.ping_interval = lim.ping_interval_ms;
    cfg.ping_timeout = lim.ping_timeout_ms;
    cfg.max_payload = lim.max_payload_bytes;
    co_await respond(w, 200, eio::handshake_packet(cfg));
}

asio::awaitable<void> SocketIoServer::handle_poll(Request& req, ResponseWriter& w,
                                                  std::shared_ptr<Connection> conn) {
    (void)req;
    std::string body = co_await conn->drain_for_poll();
    co_await respond(w, 200, std::move(body));
}

asio::awaitable<void> SocketIoServer::handle_post(Request& req, ResponseWriter& w,
                                                  std::shared_ptr<Connection> conn) {
    co_await conn->ingest(std::move(req.body));
    co_await respond(w, 200, "ok");
}

asio::awaitable<void> SocketIoServer::handle_ws_upgrade(Request& req, ResponseWriter& w,
                                                        std::shared_ptr<Connection> conn) {
    net::HijackedConnection hc = w.hijack();
    websocket::stream<beast::tcp_stream> ws(std::move(hc.stream));
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    co_await ws.async_accept(rebuild_request(req), asio::use_awaitable);
    // Drive the WebSocket on the connection's strand. The underlying socket's
    // executor is the bare (multi-threaded) io_context, so without this the
    // concurrent ws_reader/ws_writer hop across threads on the *same* stream,
    // violating Beast's single-strand contract and racing the heartbeat loop's
    // access to outbox_/awaiting_pong_/the timers. co_spawning on the strand makes
    // use_awaitable's associated executor the strand, so Beast dispatches its
    // intermediate handlers there too — serializing all stream ops and state.
    co_await asio::co_spawn(conn->strand(), conn->run_websocket(std::move(ws)),
                            asio::use_awaitable);
}

std::shared_ptr<Connection> SocketIoServer::find(const std::string& sid) {
    std::shared_lock lock(conn_mu_);
    auto it = by_sid_.find(sid);
    return it == by_sid_.end() ? nullptr : it->second;
}

void SocketIoServer::on_connection(std::shared_ptr<Socket> socket, const std::string& address) {
    // Enforce the per-IP connection + rate limit before running game logic.
    if (!register_ip_connection(address)) {
        socket->emit("ForceDisconnect", "ErrorRateLimited");
        socket->disconnect();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(ip_mu_);
        ip_by_sid_[socket->id()] = address;
    }
    if (connect_handler_) connect_handler_(std::move(socket));
}

bool SocketIoServer::register_ip_connection(const std::string& address) {
    TransportLimits lim = limits();
    const int conn_limit = lim.ip_connection_limit;
    const int rate_limit = lim.ip_connection_rate_per_sec;
    std::lock_guard<std::mutex> lock(ip_mu_);
    auto& v = ip_connections_[address];
    std::int64_t now = now_ms();
    bool over_rate =
        static_cast<int>(v.size()) >= rate_limit && now - v[v.size() - rate_limit] <= 1000;
    if (static_cast<int>(v.size()) >= conn_limit || over_rate) {
        if (v.empty()) ip_connections_.erase(address);
        return false;
    }
    v.push_back(now);
    return true;
}

void SocketIoServer::release_ip_connection(const std::string& address) {
    std::lock_guard<std::mutex> lock(ip_mu_);
    auto it = ip_connections_.find(address);
    if (it == ip_connections_.end()) return;
    if (it->second.size() <= 1)
        ip_connections_.erase(it);
    else
        it->second.pop_front();
}

void SocketIoServer::remove(const std::string& sid) {
    {
        std::unique_lock lock(conn_mu_);
        by_sid_.erase(sid);
    }
    std::string address;
    {
        std::lock_guard<std::mutex> lock(ip_mu_);
        auto it = ip_by_sid_.find(sid);
        if (it != ip_by_sid_.end()) {
            address = it->second;
            ip_by_sid_.erase(it);
        }
    }
    if (!address.empty()) release_ip_connection(address);
}

void SocketIoServer::join_room(const std::string& room, const std::string& sid) {
    std::unique_lock lock(room_mu_);
    rooms_[room].insert(sid);
}

void SocketIoServer::leave_room(const std::string& room, const std::string& sid) {
    std::unique_lock lock(room_mu_);
    auto it = rooms_.find(room);
    if (it == rooms_.end()) return;
    it->second.erase(sid);
    if (it->second.empty()) rooms_.erase(it);
}

void SocketIoServer::emit_to_room(const std::string& room, std::string_view event,
                                  const nlohmann::json& data, const std::string& except_sid) {
    std::vector<std::string> members;
    {
        std::shared_lock lock(room_mu_);
        auto it = rooms_.find(room);
        if (it != rooms_.end()) members.assign(it->second.begin(), it->second.end());
    }
    for (const auto& sid : members) {
        if (sid == except_sid) continue;
        std::shared_ptr<Connection> conn;
        {
            std::shared_lock lock(conn_mu_);
            auto it = by_sid_.find(sid);
            if (it != by_sid_.end()) conn = it->second;
        }
        if (conn) conn->emit(event, data);
    }
}

void SocketIoServer::emit_to_all(std::string_view event, const nlohmann::json& data) {
    std::vector<std::shared_ptr<Connection>> conns;
    {
        std::shared_lock lock(conn_mu_);
        conns.reserve(by_sid_.size());
        for (const auto& kv : by_sid_) conns.push_back(kv.second);
    }
    for (const auto& conn : conns) conn->emit(event, data);
}

std::size_t SocketIoServer::online_count() {
    std::shared_lock lock(conn_mu_);
    return by_sid_.size();
}

std::size_t SocketIoServer::room_count() {
    std::shared_lock lock(room_mu_);
    return rooms_.size();
}

void SocketIoServer::disconnect_all() {
    std::vector<std::shared_ptr<Connection>> conns;
    {
        std::shared_lock lock(conn_mu_);
        conns.reserve(by_sid_.size());
        for (const auto& kv : by_sid_) conns.push_back(kv.second);
    }
    for (const auto& conn : conns) conn->disconnect();
}

}  // namespace sbc::server::gameserver::socketio
