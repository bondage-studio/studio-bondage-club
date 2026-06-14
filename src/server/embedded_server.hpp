#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace sbc::config {
class Store;
}

namespace sbc::server {

// EmbeddedServer bundles the full runtime an in-process host needs — the Asio
// runtime + worker threads, the blocking pool, the TLS context, the App router
// and the HTTP listener — behind a simple start()/stop() pair.
//
// It is the shared core of both the desktop entry point (src/main.cpp, which
// wraps it in a signal-wait loop) and the Android JNI bridge (which starts it on
// onCreate and stops it on onDestroy). start() is non-blocking: HttpServer::start
// only binds the acceptor and spawns the accept loop on the IoRuntime worker
// threads, so control returns to the caller while the server keeps running.
class EmbeddedServer {
public:
    EmbeddedServer();
    ~EmbeddedServer();

    EmbeddedServer(const EmbeddedServer&) = delete;
    EmbeddedServer& operator=(const EmbeddedServer&) = delete;

    // start opens the config store at config_path (empty -> platform default
    // dir), loads/normalises the config, optionally overrides the listen host
    // (when non-empty) and port (when non-zero), and starts the HTTP listener.
    // Throws std::exception on config-load, app-construction or bind failure.
    // The bound "host:port" is returned.
    std::string start(const std::string& config_path, const std::string& host_override,
                      std::uint16_t port_override);

    // stop tears everything down in reverse order. Safe to call once, after a
    // successful start(); the destructor calls it if start() succeeded.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sbc::server
