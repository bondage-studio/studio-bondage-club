#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "server/embedded_server.hpp"

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

    server::EmbeddedServer server;
    try {
        server.start(config_path, /*host_override=*/"", /*port_override=*/0);
    } catch (const std::exception& e) {
        spdlog::error("start error={}", e.what());
        return 1;
    }

    // Wait for SIGINT/SIGTERM, then shut down. The signal_set needs its own
    // io_context: EmbeddedServer's runtime is busy serving requests, and running
    // the signal wait on a private context keeps the lifecycle plumbing out of
    // the reusable server core.
    asio::io_context signal_ctx;
    std::mutex m;
    std::condition_variable cv;
    bool stop = false;
    asio::signal_set signals(signal_ctx, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        cv.notify_one();
    });

    std::thread signal_thread([&] { signal_ctx.run(); });

    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return stop; });
    }

    server.stop();
    signal_ctx.stop();
    signal_thread.join();
    return 0;
}
