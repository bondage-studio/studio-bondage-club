#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include "cache/flight.hpp"
#include "cache/metadata.hpp"
#include "cache/router.hpp"
#include "common/url.hpp"
#include "config/config.hpp"
#include "host/provider.hpp"
#include "host/provider_context.hpp"

namespace sbc::net {
class HttpClient;
}

namespace sbc::host::reverseproxy {

// FetchResult is the outcome of a cache fetch/refresh: either a stored entry
// (served from a backend) or an uncacheable temp file streamed once.
struct FetchResult;

class Provider : public host::Provider,
                 public host::HomepageProvider,
                 public host::RemoteProxyProvider,
                 public host::StoreProvider,
                 public host::LiveUpdater,
                 public std::enable_shared_from_this<Provider> {
public:
    static std::shared_ptr<Provider> create(const config::Config& cfg,
                                            const host::ProviderContext& ctx);

    boost::asio::awaitable<void> serve(server::Request& req, server::ResponseWriter& w) override;
    host::RuntimeStatus status() const override;
    void close() override;

    boost::asio::awaitable<host::HomepageDocument> fetch_homepage(
        const server::Request& req) override;
    boost::asio::awaitable<void> serve_remote_http(server::Request& req, server::ResponseWriter& w,
                                                   const Url& target) override;
    std::vector<std::shared_ptr<cache::Backend>> all_stores() override;
    void live_update(const config::Config& cfg) override;

private:
    Provider() = default;

    // Snapshot is the live-updatable state, published atomically.
    struct Snapshot {
        Url upstream;
        std::string socks5_proxy;
        std::shared_ptr<net::HttpClient> client;
        std::chrono::seconds default_ttl{0};
        std::int64_t default_max = 0;
        std::unordered_map<std::string, std::chrono::seconds> store_ttl;
        std::unordered_map<std::string, std::int64_t> store_max;
    };

    std::shared_ptr<const Snapshot> snapshot() const;
    std::shared_ptr<Snapshot> build_snapshot(const config::Config& cfg) const;

    std::tuple<std::shared_ptr<cache::Backend>, std::chrono::seconds, std::int64_t> resolve_store(
        const Snapshot& snap, const cache::RouteAction& action) const;
    std::string cache_key(const Url& upstream, const Url& target,
                          const cache::RouteAction& action) const;
    Url target_url(const Url& upstream, const server::Request& req) const;

    boost::asio::awaitable<void> serve_target(server::Request& req, server::ResponseWriter& w,
                                              const Snapshot& snap, Url target);
    boost::asio::awaitable<std::shared_ptr<FetchResult>> fetch(
        const Snapshot& snap, Url target, std::string key, std::shared_ptr<cache::Backend> store,
        std::chrono::seconds ttl, std::int64_t max_bytes, bool force_cache,
        std::string cache_control, std::optional<cache::Metadata> cached, server::Request& req);
    boost::asio::awaitable<void> proxy_pass(server::Request& req, server::ResponseWriter& w,
                                            const Snapshot& snap, const Url& target,
                                            std::string cache_status);
    boost::asio::awaitable<void> serve_entry(server::Request& req, server::ResponseWriter& w,
                                             std::shared_ptr<cache::Backend> store,
                                             const cache::Metadata& meta, std::string cache_status,
                                             std::string cache_control, bool stale_warning);
    boost::asio::awaitable<void> serve_result(server::Request& req, server::ResponseWriter& w,
                                              const FetchResult& result);

    void schedule_touch(const std::shared_ptr<cache::Backend>& store, const std::string& key,
                        cache::TimePoint now);

    host::ProviderContext ctx_;
    std::string cache_dir_;
    std::unordered_map<std::string, std::shared_ptr<cache::Backend>> stores_;
    std::shared_ptr<cache::PolicyRouter> router_ = std::make_shared<cache::PolicyRouter>();
    cache::FlightGroup<std::shared_ptr<FetchResult>> flights_;

    mutable std::shared_mutex mu_;
    std::shared_ptr<const Snapshot> snap_;
};

}  // namespace sbc::host::reverseproxy
