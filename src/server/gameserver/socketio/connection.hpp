#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <nlohmann/json.hpp>

namespace sbc::server::gameserver::socketio {

class Socket;
class SocketIoServer;

// EventHandler receives the first argument of a Socket.IO event (args[1], or null
// when the event carried no payload). BC never uses ack callbacks, so there is no
// ack parameter.
using EventHandler = std::function<void(const nlohmann::json& data)>;
using DisconnectHandler = std::function<void(const std::string& reason)>;
using AnyHandler = std::function<void(const std::string& event, const nlohmann::json& data)>;

// Connection is the per-Engine.IO-session object. It owns a strand; every
// mutation of its outbox / handlers / rooms / timers runs on that strand, so no
// per-connection lock is needed. Inbound HTTP calls (drain_for_poll / ingest) hop
// onto the strand; outbound calls (enqueue_eio / emit) post to it from any thread.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(std::string sid, boost::asio::any_io_executor ex, SocketIoServer* hub,
               std::string address);

    const std::string& sid() const { return sid_; }
    const std::string& address() const { return address_; }

    // start spawns the heartbeat loop. Call once, after registering the
    // connection with the hub.
    void start();

    // drain_for_poll services an Engine.IO long-poll GET: returns the next batch
    // of queued packets, parking until one is available (or the connection
    // closes). Hops onto the strand internally.
    boost::asio::awaitable<std::string> drain_for_poll();

    // ingest processes an Engine.IO long-poll POST body. Hops onto the strand.
    boost::asio::awaitable<void> ingest(std::string body);

    // run_websocket drives an upgraded WebSocket transport: it performs the
    // Engine.IO probe handshake (2probe/3probe, then "5"), flips the connection
    // to WebSocket mode, and pumps frames until close. Runs in the hijacked
    // session's coroutine. If the probe never completes the connection stays on
    // long-polling and this returns without disconnecting.
    boost::asio::awaitable<void> run_websocket(
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws);

    // enqueue_eio appends a fully-encoded Engine.IO packet to the outbox and wakes
    // a parked poll. Safe to call from any thread.
    void enqueue_eio(std::string packet);

    // emit sends a Socket.IO EVENT to this connection.
    void emit(std::string_view event, const nlohmann::json& data);

    // Handler registration. Called on the strand (during the connect handler).
    void on(std::string event, EventHandler fn);
    void off(const std::string& event);
    void set_disconnect_handler(DisconnectHandler fn);
    void set_any_handler(AnyHandler fn);

    // disconnect closes the connection from the server side.
    void disconnect();

    // Room membership bookkeeping (the hub owns the room->member index; these
    // track the inverse for cleanup). Run on the strand.
    void note_joined(const std::string& room);
    void note_left(const std::string& room);
    const std::set<std::string>& rooms() const { return rooms_; }

    boost::asio::strand<boost::asio::any_io_executor>& strand() { return strand_; }

private:
    void push_locked(std::string packet);   // append + wake; must run on strand
    void handle_sio_payload(std::string_view payload);              // on strand
    void dispatch_event(const std::string& event, const nlohmann::json& data);  // on strand
    void do_disconnect(const std::string& reason);                  // on strand
    boost::asio::awaitable<void> heartbeat_loop();
    boost::asio::awaitable<void> ws_reader(
        boost::beast::websocket::stream<boost::beast::tcp_stream>& ws);
    boost::asio::awaitable<void> ws_writer(
        boost::beast::websocket::stream<boost::beast::tcp_stream>& ws);

    std::string sid_;
    std::string address_;  // peer IP, for the connection/message rate limits
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    SocketIoServer* hub_;
    boost::asio::steady_timer park_timer_;
    boost::asio::steady_timer ping_timer_;
    std::deque<std::string> outbox_;
    bool sio_connected_ = false;
    bool closing_ = false;
    bool awaiting_pong_ = false;
    bool ws_mode_ = false;   // true once the WebSocket upgrade completed
    bool ws_stop_ = false;   // signals the WS read/write loops to exit
    std::shared_ptr<Socket> socket_;
    std::unordered_map<std::string, EventHandler> handlers_;
    DisconnectHandler disconnect_handler_;
    AnyHandler any_handler_;
    std::set<std::string> rooms_;
    std::deque<std::int64_t> msg_bucket_;  // sliding window for the message rate limit
};

}  // namespace sbc::server::gameserver::socketio
