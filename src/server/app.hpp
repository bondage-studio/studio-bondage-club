#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "config/store.hpp"
#include "host/provider.hpp"
#include "host/provider_context.hpp"
#include "server/config_scope.hpp"
#include "server/gameserver/game/game_app.hpp"
#include "server/http_types.hpp"
#include "server/static_assets.hpp"

namespace sbc::server {

// App is the top-level request router and lifecycle owner, the C++ analog of Go
// server.App. It holds the active config + provider behind a snapshot and serves
// /api, the homepage shell, static assets, socket.io, the remote loader, and
// delegates everything else to the active provider.
class App {
public:
    App(config::Store& store, config::Config cfg, host::ProviderContext ctx);

    Handler handler();

    boost::asio::awaitable<void> serve(Request& req, ResponseWriter& raw);

    void close();

    std::string active_address() const { return active_address_; }

private:
    struct State {
        config::Config cfg;
        std::shared_ptr<host::Provider> provider;
    };

    std::shared_ptr<const State> snapshot() const;

    boost::asio::awaitable<void> handle_api(Request& req, ResponseWriter& w, const State& state);
    boost::asio::awaitable<void> handle_get_config(ResponseWriter& w);
    boost::asio::awaitable<void> handle_put_config(Request& req, ResponseWriter& w);
    boost::asio::awaitable<void> handle_put_config_scope(Request& req, ResponseWriter& w,
                                                         const std::string& scope_name);
    boost::asio::awaitable<void> handle_reset_config(ResponseWriter& w);
    boost::asio::awaitable<void> handle_sse(ResponseWriter& w);
    boost::asio::awaitable<void> handle_get_homepage(Request& req, ResponseWriter& w);

    boost::asio::awaitable<cache::Stats> cache_stats();
    boost::asio::awaitable<void> clear_cache();
    // expire_cache soft-expires matching entries across stores (keeps bodies so
    // the next request revalidates). Empty filters match anything. Returns count.
    boost::asio::awaitable<int> expire_cache(const std::string& store, const std::string& host,
                                             const std::string& path_prefix,
                                             const std::string& version);
    // cache_versions returns version labels + counts, optionally limited to one
    // store (empty = aggregate across all stores).
    boost::asio::awaitable<std::vector<std::pair<std::string, int>>> cache_versions(
        const std::string& store);

    static std::shared_ptr<host::Provider> provider_for(const config::Config& cfg,
                                                        const host::ProviderContext& ctx);

    // Scope-based reload. register_scopes() builds the scope table once.
    // apply_config installs new_cfg, firing reload work only for the scopes whose
    // slice changed (restricted to `only_scope` when set), and returns each
    // changed scope's tier. Throws on provider/store failure (caller restores).
    void register_scopes();
    const ConfigScope* find_scope(const std::string& name) const;
    std::map<std::string, UpdateTier> apply_config(const config::Config& new_cfg,
                                                   const std::string& only_scope, bool migrate_cache);

    config::Store& store_;
    host::ProviderContext ctx_;
    std::string active_address_;
    std::shared_ptr<AssetSource> assets_;
    std::vector<ConfigScope> scopes_;

    // game_ is the embedded local game server. It is a direct member (outside
    // State) so toggling localGameServer at runtime never tears it down.
    std::shared_ptr<gameserver::GameApp> game_;

    mutable std::shared_mutex state_mu_;
    std::shared_ptr<const State> state_;
};

}  // namespace sbc::server
