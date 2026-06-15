#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "server/http_types.hpp"

namespace sbc::net {
class IoRuntime;
}

namespace sbc::server {

// ConnectionStats tracks live connection counts. Shared (by reference) with
// every Session.
struct ConnectionStats {
    std::atomic<std::int64_t> accepted{0};
    std::atomic<std::int64_t> active{0};    // currently open connections
    std::atomic<std::int64_t> handling{0};  // currently processing a request
};

// HttpServer owns the TCP acceptor and spawns a Session coroutine per
// connection. It listens on plain TCP (local admin/proxy endpoint).
class HttpServer {
public:
    HttpServer(net::IoRuntime& runtime, std::string host, std::uint16_t port, Handler handler);

    // start binds + listens and spawns the accept loop. Throws on bind failure.
    void start();
    // stop closes the acceptor (in-flight sessions finish on their own).
    void stop();

    const ConnectionStats& stats() const { return *stats_; }
    std::string address() const { return address_; }

private:
    boost::asio::awaitable<void> accept_loop();

    net::IoRuntime& runtime_;
    std::string host_;
    std::uint16_t port_;
    Handler handler_;
    std::string address_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<ConnectionStats> stats_;
};

}  // namespace sbc::server
