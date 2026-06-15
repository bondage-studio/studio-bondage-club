#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server::gameserver::socketio {

class Connection;
class Socket;

// TransportLimits carries the Engine.IO/rate-limit knobs the transport layer
// needs. It is a plain struct (no config dependency) so the socketio layer stays
// decoupled from the game; GameApp pushes updated values via set_limits().
struct TransportLimits {
    int ping_interval_ms = 50000;
    int ping_timeout_ms = 30000;
    int max_payload_bytes = 180000;
    int message_rate_per_sec = 20;
    int ip_connection_limit = 64;
    int ip_connection_rate_per_sec = 2;
};

// SocketIoServer is the connection hub: it owns the Engine.IO/Socket.IO session
// registry and the room index, dispatches inbound HTTP requests to the right
// connection, and fans out broadcasts. Layering: it knows nothing about the game;
// game logic is installed via the connect handler.
class SocketIoServer {
public:
    using ConnectHandler = std::function<void(std::shared_ptr<Socket>)>;

    explicit SocketIoServer(boost::asio::any_io_executor ex);

    // set_connect_handler installs the per-connection setup callback (invoked on
    // the connection's strand when a Socket.IO CONNECT completes).
    void set_connect_handler(ConnectHandler h) { connect_handler_ = std::move(h); }

    // set_limits / limits expose the live transport knobs. GameApp pushes updates
    // whenever the game settings change; the transport layer reads a snapshot at
    // the point of use so changes apply with no teardown.
    void set_limits(TransportLimits l);
    TransportLimits limits() const;

    boost::asio::any_io_executor executor() const { return ex_; }

    // handle_request is the single HTTP entry point for "/socket.io/*". It routes
    // the Engine.IO handshake, long-poll traffic, and WebSocket upgrades.
    boost::asio::awaitable<void> handle_request(Request& req, ResponseWriter& w);

    std::shared_ptr<Connection> find(const std::string& sid);

    // Invoked by Connection on a completed CONNECT / on teardown. `address` is the
    // peer IP, used for the per-IP connection/rate limit.
    void on_connection(std::shared_ptr<Socket> socket, const std::string& address);
    void remove(const std::string& sid);

    // Room membership. join/leave update the room index; the broadcasters resolve
    // members under a shared lock then post to each connection's strand (no lock
    // held during I/O).
    void join_room(const std::string& room, const std::string& sid);
    void leave_room(const std::string& room, const std::string& sid);

    void emit_to_room(const std::string& room, std::string_view event,
                      const nlohmann::json& data, const std::string& except_sid = {});
    void emit_to_all(std::string_view event, const nlohmann::json& data);

    std::size_t online_count();
    std::size_t room_count();

    void disconnect_all();

private:
    boost::asio::awaitable<void> handle_handshake(Request& req, ResponseWriter& w);
    boost::asio::awaitable<void> handle_poll(Request& req, ResponseWriter& w,
                                             std::shared_ptr<Connection> conn);
    boost::asio::awaitable<void> handle_post(Request& req, ResponseWriter& w,
                                             std::shared_ptr<Connection> conn);
    boost::asio::awaitable<void> handle_ws_upgrade(Request& req, ResponseWriter& w,
                                                   std::shared_ptr<Connection> conn);

    boost::asio::any_io_executor ex_;
    ConnectHandler connect_handler_;

    mutable std::mutex limits_mu_;
    TransportLimits limits_;

    std::shared_mutex conn_mu_;
    std::unordered_map<std::string, std::shared_ptr<Connection>> by_sid_;

    std::shared_mutex room_mu_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rooms_;

    // Per-IP connection tracking for the connection/rate limit. Each entry is
    // the list of active connection timestamps for that address.
    bool register_ip_connection(const std::string& address);
    void release_ip_connection(const std::string& address);
    std::mutex ip_mu_;
    std::unordered_map<std::string, std::deque<std::int64_t>> ip_connections_;
    std::unordered_map<std::string, std::string> ip_by_sid_;  // sid -> address (accepted only)
};

}  // namespace sbc::server::gameserver::socketio
