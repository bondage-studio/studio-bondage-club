#include "server/http_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include "net/io_runtime.hpp"
#include "server/session.hpp"

namespace sbc::server {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

HttpServer::HttpServer(net::IoRuntime& runtime, std::string host, std::uint16_t port,
                       Handler handler)
    : runtime_(runtime),
      host_(std::move(host)),
      port_(port),
      handler_(std::move(handler)),
      acceptor_(runtime.context()),
      stats_(std::make_shared<ConnectionStats>()) {
    address_ = host_ + ":" + std::to_string(port_);
}

void HttpServer::start() {
    boost::system::error_code ec;
    auto addr = asio::ip::make_address(host_, ec);
    tcp::endpoint endpoint = ec ? tcp::endpoint(tcp::v4(), port_) : tcp::endpoint(addr, port_);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);

    asio::co_spawn(runtime_.context(), accept_loop(), asio::detached);
}

void HttpServer::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

asio::awaitable<void> HttpServer::accept_loop() {
    for (;;) {
        boost::system::error_code ec;
        tcp::socket socket(runtime_.context());
        co_await acceptor_.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            if (ec == asio::error::operation_aborted) break;  // acceptor closed
            spdlog::warn("accept failed error={}", ec.message());
            continue;
        }
        stats_->accepted.fetch_add(1, std::memory_order_relaxed);
        auto session = std::make_shared<Session>(std::move(socket), handler_, *stats_);
        asio::co_spawn(
            runtime_.context(), [session]() -> asio::awaitable<void> { co_await session->run(); },
            asio::detached);
    }
}

}  // namespace sbc::server
