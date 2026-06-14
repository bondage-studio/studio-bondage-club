#pragma once

#include <memory>
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

// Provider is the active serving backend (reverse-proxy or package mode), the
// C++ analog of Go's host.Provider. Capability mix-ins below are queried via
// dynamic_cast, mirroring Go's interface assertions.
class Provider {
public:
    virtual ~Provider() = default;
    virtual boost::asio::awaitable<void> serve(server::Request& req,
                                               server::ResponseWriter& w) = 0;
    virtual RuntimeStatus status() const = 0;
    virtual void close() {}
};

// HomepageProvider fetches the upstream homepage source (GET /api/homepage).
class HomepageProvider {
public:
    virtual ~HomepageProvider() = default;
    virtual boost::asio::awaitable<HomepageDocument> fetch_homepage(
        const server::Request& req) = 0;
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
