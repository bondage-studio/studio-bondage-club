#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include "cache/backend.hpp"
#include "common/http_util.hpp"
#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc {
class Url;
}
namespace sbc::config {
struct Config;
}

namespace sbc::host {

struct Capability {
    std::string id;
    std::string label;
    bool enabled = false;
    std::string description;
};

struct RuntimeStatus {
    std::string mode;
    std::string upstream;   // omitted from JSON when empty
    std::string cache_dir;  // omitted from JSON when empty
    std::vector<Capability> capabilities;
};

struct HomepageDocument {
    std::string url;
    int status_code = 0;
    HeaderMap header;
    std::string body;
    std::string cache_status;
};

// Provider is the active serving backend (reverse-proxy or package mode).
// Capability mix-ins below are queried via dynamic_cast.
class Provider {
public:
    virtual ~Provider() = default;
    virtual boost::asio::awaitable<void> serve(server::Request& req, server::ResponseWriter& w) = 0;
    virtual RuntimeStatus status() const = 0;
    virtual void close() {}
};

// HomepageProvider fetches the upstream homepage source (the `homepage.get` RPC).
class HomepageProvider {
public:
    virtual ~HomepageProvider() = default;
    virtual boost::asio::awaitable<HomepageDocument> fetch_homepage(const server::Request& req) = 0;
};

// RemoteProxyProvider serves arbitrary remote URLs via the cache path
// (the /http(s)://... remote loader).
class RemoteProxyProvider {
public:
    virtual ~RemoteProxyProvider() = default;
    virtual boost::asio::awaitable<void> serve_remote_http(server::Request& req,
                                                           server::ResponseWriter& w,
                                                           const Url& target) = 0;
};

// CacheHit is a confirmed, fresh cache HIT produced by CacheProbeProvider for
// in-process hosts (the desktop CEF client) that serve cached bytes directly
// instead of looping the request back through the local HTTP server. It carries
// the serve-ready response head (the same headers serve_entry would emit, minus
// Content-Length) plus a handle to read the body lazily off the hot path.
struct CacheHit {
    int status_code = 0;
    HeaderMap header;                       // serve-ready (no Content-Length/Vary)
    std::shared_ptr<cache::Backend> store;  // body source for read_cache_body
    std::string key;
    std::int64_t body_size = 0;
};

// CacheProbeProvider answers "would this GET be served straight from a fresh
// cache entry?" synchronously, doing no upstream/network work. Only a clean,
// fresh, unconditional HIT returns a value; anything that would revalidate,
// stream, range, or miss returns nullopt so the caller falls back to the normal
// (looped-back) serve path. Implemented by the reverse-proxy provider; queried
// via dynamic_cast on the active provider.
class CacheProbeProvider {
public:
    virtual ~CacheProbeProvider() = default;
    // |explicit_target| is the remote-loader target (the /http(s)://... loader)
    // when set; when null the target is derived from |req| exactly as
    // Provider::serve does. Records the HIT into the traffic stats on success
    // (the looped-back serve_entry would have). Safe to call from any thread.
    virtual std::optional<CacheHit> probe_cache_hit(const server::Request& req,
                                                    const Url* explicit_target) = 0;
    // read_cache_body reads a committed body by store+key (a synchronous DB read;
    // call off the latency-sensitive thread). Returns "" if the entry vanished.
    virtual std::string read_cache_body(const std::shared_ptr<cache::Backend>& store,
                                        const std::string& key) = 0;
};

// StoreProvider exposes the cache backends for stats/clear/SSE.
class StoreProvider {
public:
    virtual ~StoreProvider() = default;
    virtual std::vector<std::shared_ptr<cache::Backend>> all_stores() = 0;
};

// LiveUpdater applies a tier-1 (live) config change in place.
class LiveUpdater {
public:
    virtual ~LiveUpdater() = default;
    virtual void live_update(const config::Config& cfg) = 0;
};

}  // namespace sbc::host
