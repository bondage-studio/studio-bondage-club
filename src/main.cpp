#include <boost/asio/signal_set.hpp>

#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "config/config.hpp"
#include "config/store.hpp"
#include "host/provider_context.hpp"
#include "net/blocking_pool.hpp"
#include "net/io_runtime.hpp"
#include "net/tls.hpp"
#include "server/app.hpp"
#include "server/http_server.hpp"

namespace asio = boost::asio;
using namespace sbc;

namespace {

// parse_config_flag extracts -config/--config <path> from argv (the only flag,
// mirroring the Go CLI).
std::string parse_config_flag(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-config" || args[i] == "--config") && i + 1 < args.size()) {
            return args[i + 1];
        }
        const std::string prefix = "-config=";
        const std::string prefix2 = "--config=";
        if (args[i].rfind(prefix, 0) == 0) return args[i].substr(prefix.size());
        if (args[i].rfind(prefix2, 0) == 0) return args[i].substr(prefix2.size());
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    spdlog::set_pattern("time=%Y-%m-%dT%H:%M:%SZ level=%l %v", spdlog::pattern_time_type::utc);

    std::string config_path = parse_config_flag(argc, argv);

    config::Store store = config::Store::open(config_path);
    config::Config cfg;
    try {
        cfg = store.load();
    } catch (const std::exception& e) {
        spdlog::error("load config error={}", e.what());
        return 1;
    }

    net::IoRuntime runtime;
    net::BlockingPool blocking;
    net::TlsContext tls;

    host::ProviderContext ctx;
    ctx.io = &runtime;
    ctx.blocking = &blocking;
    ctx.tls = &tls;

    std::unique_ptr<server::App> app;
    try {
        app = std::make_unique<server::App>(store, cfg, ctx);
    } catch (const std::exception& e) {
        spdlog::error("create app error={}", e.what());
        return 1;
    }

    server::HttpServer http_server(runtime, cfg.server.host,
                                   static_cast<std::uint16_t>(cfg.server.port), app->handler());
    try {
        http_server.start();
    } catch (const std::exception& e) {
        spdlog::error("http server failed error={}", e.what());
        return 1;
    }

    spdlog::info("studio bondage club local host started address={} config={} mode={} upstream={}",
                 "http://" + http_server.address(), store.path().string(), cfg.mode, cfg.upstream);

    std::mutex m;
    std::condition_variable cv;
    bool stop = false;
    asio::signal_set signals(runtime.context(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return stop; });
    }

    spdlog::info("shutting down");
    http_server.stop();
    app->close();
    runtime.stop();
    blocking.stop();
    blocking.join();

    spdlog::info("studio bondage club local host stopped");
    return 0;
}
