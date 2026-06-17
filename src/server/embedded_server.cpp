#include "server/embedded_server.hpp"

#include <memory>
#include <mutex>
#include <utility>

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "config/config.hpp"
#include "config/store.hpp"
#include "host/provider_context.hpp"
#include "net/blocking_pool.hpp"
#include "net/io_runtime.hpp"
#include "net/tls.hpp"
#include "server/app.hpp"
#include "server/http_server.hpp"
#include "server/rpc/session.hpp"

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

    // Native RPC bridge state (Android). The session is created lazily on the
    // first authenticated frame; native_mu guards its lifecycle against the
    // host thread (deliver/reset) racing the Asio sender.
    std::mutex native_mu;
    std::function<void(std::string)> native_sender;
    std::shared_ptr<rpc::RpcSession> native_session;

    explicit Impl(config::Store s) : store(std::move(s)) {}
};

EmbeddedServer::EmbeddedServer() = default;

EmbeddedServer::~EmbeddedServer() {
    if (impl_) {
        stop();
    }
}

std::string EmbeddedServer::start(const std::string& config_path, const std::string& host_override,
                                  std::uint16_t port_override) {
    config::Store store = config::Store::open(config_path);
    config::Config cfg = store.load();

    if (!host_override.empty()) {
        cfg.server.host = host_override;
    }
    if (port_override != 0) {
        cfg.server.port = port_override;
    }

#if defined(__ANDROID__)
    hardware_acceleration_ = cfg.android.hardware_acceleration;
#endif
#if defined(SBC_DESKTOP)
    desktop_ = cfg.desktop;
#endif

    auto impl = std::make_unique<Impl>(std::move(store));

    host::ProviderContext ctx;
    ctx.io = &impl->runtime;
    ctx.blocking = &impl->blocking;
    ctx.tls = &impl->tls;

    impl->app = std::make_unique<App>(impl->store, cfg, ctx);
    impl->http_server = std::make_unique<HttpServer>(impl->runtime, cfg.server.host,
                                                     static_cast<std::uint16_t>(cfg.server.port),
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

#if defined(SBC_NATIVE_RPC)

namespace asio = boost::asio;

void EmbeddedServer::set_rpc_sender(std::function<void(std::string)> sender) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->native_mu);
    impl_->native_sender = std::move(sender);
}

void EmbeddedServer::deliver_rpc_frame(std::string frame) {
    if (!impl_) return;

    auto msg = nlohmann::ordered_json::parse(frame, nullptr, /*allow_exceptions=*/false);
    if (msg.is_discarded() || !msg.is_object()) return;

    // Hard boundary: verify the capability token on every frame, then strip it so
    // the session never sees it. A bad/absent token drops the frame silently
    // (the WS path closes 4401; a one-shot bridge has nothing to close).
    if (!impl_->app->rpc_verify(msg.value("token", std::string{}))) return;
    msg.erase("token");

    std::shared_ptr<rpc::RpcSession> session;
    {
        std::lock_guard<std::mutex> lock(impl_->native_mu);
        if (!impl_->native_session) {
            if (!impl_->native_sender) return;  // no sink wired yet
            auto sender = impl_->native_sender;
            impl_->native_session = impl_->app->make_rpc_session(
                asio::make_strand(impl_->runtime.executor()), std::move(sender));
        }
        session = impl_->native_session;
    }

    // Hop onto the session's strand: handle_frame touches the subscription map.
    asio::post(session->executor(),
               [session, m = std::move(msg)]() mutable { session->handle_frame(m); });
}

void EmbeddedServer::reset_rpc() {
    if (!impl_) return;
    std::shared_ptr<rpc::RpcSession> session;
    {
        std::lock_guard<std::mutex> lock(impl_->native_mu);
        session = std::move(impl_->native_session);
        impl_->native_session.reset();
    }
    if (session) {
        asio::post(session->executor(), [session] { session->cancel_all(); });
    }
}

#endif  // SBC_NATIVE_RPC

#if defined(SBC_DESKTOP)

namespace asio_desktop = boost::asio;

void EmbeddedServer::on_config_change(
    ConfigPhase phase, std::string scope_filter,
    std::function<void(const config::Config&, const config::Config&)> cb) {
    if (!impl_ || !impl_->app) return;
    impl_->app->on_config_change(
        phase, std::move(scope_filter),
        [cb = std::move(cb)](const ConfigChange& ch) { cb(ch.old_cfg, ch.new_cfg); });
}

void EmbeddedServer::update_desktop_window_size(int width, int height) {
    if (!impl_ || !impl_->app) return;
    App* app = impl_->app.get();
    asio_desktop::post(impl_->runtime.executor(),
                       [app, width, height] { app->apply_desktop_window_size(width, height); });
}

#endif  // SBC_DESKTOP

}  // namespace sbc::server
