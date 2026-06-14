#pragma once

#include <memory>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include "net/stream.hpp"
#include "server/http_server.hpp"
#include "server/http_types.hpp"

namespace sbc::server {

// Session drives a single keep-alive HTTP/1.1 connection: read request, build a
// framework-agnostic Request, invoke the Handler with a Beast-backed
// ResponseWriter, then loop until close/timeout/error or a hijack.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket socket, Handler handler, ConnectionStats& stats);

    boost::asio::awaitable<void> run();

private:
    net::TcpStream stream_;
    boost::beast::flat_buffer buffer_;
    Handler handler_;
    ConnectionStats& stats_;
};

}  // namespace sbc::server
