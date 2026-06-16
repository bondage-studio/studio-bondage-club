#pragma once

#include <cstdint>
#include <functional>
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

#if defined(__ANDROID__)
    bool hardware_acceleration() const { return hardware_acceleration_; }
#endif

#if defined(SBC_NATIVE_RPC)
    // --- Native RPC bridge (native hosts: Android WebView, desktop CEF) ---
    // The native host drives the same RPC dispatcher the /rpc WebSocket uses,
    // skipping the localhost socket hop. The bridge object injected into the page
    // is globally visible to all page JS, so deliver_rpc_frame verifies the
    // capability token on every inbound frame (the stateless equivalent of the WS
    // hello check) before dispatching.

    // set_rpc_sender installs the sink for outbound frames (res/event). It is
    // invoked from Asio worker threads with a UTF-8 JSON frame; the host marshals
    // it to the WebView. Call once before delivering frames.
    void set_rpc_sender(std::function<void(std::string)> sender);

    // deliver_rpc_frame feeds one inbound JSON frame (carrying a "token") from the
    // bridge. Drops it on a token mismatch or before a sender is set. Lazily opens
    // the session on the first valid frame. Safe to call from any thread.
    void deliver_rpc_frame(std::string frame);

    // reset_rpc tears down the live native session (e.g. on navigation/destroy).
    void reset_rpc();
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#if defined(__ANDROID__)
    bool hardware_acceleration_ = true;
#endif
};

}  // namespace sbc::server
