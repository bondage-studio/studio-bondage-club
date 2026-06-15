#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <nlohmann/json.hpp>

namespace sbc::server::rpc {

class RpcDispatcher;
class RpcAuth;
class RpcSession;

// RpcConnection drives one accepted /rpc WebSocket: a token-gated hello/welcome
// handshake, then multiplexed request/response calls and subscription streams
// over a single socket.
//
// It mirrors the socket.io Connection's strand + outbox + park-timer shape so all
// stream operations and shared state stay serialized on one strand. The reader
// owns the single in-flight async_read; the writer owns the single async_write,
// draining an outbox. Request handlers and subscription tickers never touch the
// socket — they only enqueue frames and wake the writer.
class RpcConnection : public std::enable_shared_from_this<RpcConnection> {
public:
    using WsStream = boost::beast::websocket::stream<boost::beast::tcp_stream>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    RpcConnection(WsStream ws, Strand strand, const RpcDispatcher& dispatcher, const RpcAuth& auth);

    // run completes the lifetime: reader || writer, then a best-effort close.
    boost::asio::awaitable<void> run();

private:
    boost::asio::awaitable<void> writer();
    boost::asio::awaitable<void> reader();
    boost::asio::awaitable<void> handle_message(std::string text);
    void push(std::string frame);

    WsStream ws_;
    Strand strand_;
    const RpcDispatcher& dispatcher_;
    const RpcAuth& auth_;

    std::deque<std::string> outbox_;
    boost::asio::steady_timer park_timer_;
    // The req/sub/unsub mechanics live in the transport-agnostic session, created
    // once the hello handshake succeeds. Its Sender enqueues into outbox_.
    std::shared_ptr<RpcSession> session_;

    bool authed_ = false;
    bool stop_ = false;
    std::uint16_t close_code_ = 1000;  // websocket "normal"; 4401 on auth failure
    std::string close_reason_;
};

}  // namespace sbc::server::rpc
