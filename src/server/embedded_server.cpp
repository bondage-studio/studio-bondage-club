#include "server/embedded_server.hpp"

#include <memory>
#include <utility>

#include <spdlog/spdlog.h>

#include "config/config.hpp"
#include "config/store.hpp"
#include "host/provider_context.hpp"
#include "net/blocking_pool.hpp"
#include "net/io_runtime.hpp"
#include "net/tls.hpp"
#include "server/app.hpp"
#include "server/http_server.hpp"

namespace sbc::server {

// Members are declared in start order; ~Impl destroys them in reverse, but stop()
// drives the explicit shutdown sequence so teardown order matches main.cpp.
struct EmbeddedServer::Impl {
    config::Store store;
    net::IoRuntime runtime;
    net::BlockingPool blocking;
    net::TlsContext tls;
    std::unique_ptr<App> app;
    std::unique_ptr<HttpServer> http_server;

    explicit Impl(config::Store s) : store(std::move(s)) {}
};

EmbeddedServer::EmbeddedServer() = default;

EmbeddedServer::~EmbeddedServer() {
    if (impl_) {
        stop();
    }
}

std::string EmbeddedServer::start(const std::string& config_path,
                                  const std::string& host_override,
                                  std::uint16_t port_override) {
    config::Store store = config::Store::open(config_path);
    config::Config cfg = store.load();

    if (!host_override.empty()) {
        cfg.server.host = host_override;
    }
    if (port_override != 0) {
        cfg.server.port = port_override;
    }

    auto impl = std::make_unique<Impl>(std::move(store));

    host::ProviderContext ctx;
    ctx.io = &impl->runtime;
    ctx.blocking = &impl->blocking;
    ctx.tls = &impl->tls;

    impl->app = std::make_unique<App>(impl->store, cfg, ctx);
    impl->http_server = std::make_unique<HttpServer>(
        impl->runtime, cfg.server.host, static_cast<std::uint16_t>(cfg.server.port),
        impl->app->handler());
    impl->http_server->start();

    std::string address = impl->http_server->address();
    spdlog::info("studio bondage club local host started address={} config={} mode={} upstream={}",
                 "http://" + address, impl->store.path().string(), cfg.mode, cfg.upstream);

    impl_ = std::move(impl);
    return address;
}

void EmbeddedServer::stop() {
    if (!impl_) {
        return;
    }
    spdlog::info("shutting down");
    if (impl_->http_server) {
        impl_->http_server->stop();
    }
    if (impl_->app) {
        impl_->app->close();
    }
    impl_->runtime.stop();
    impl_->blocking.stop();
    impl_->blocking.join();
    impl_.reset();
    spdlog::info("studio bondage club local host stopped");
}

}  // namespace sbc::server
