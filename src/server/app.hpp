#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
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
#include "server/rpc/auth.hpp"
#include "server/rpc/dispatcher.hpp"
#include "server/rpc/session.hpp"
#include "server/static_assets.hpp"
#include "server/userscript_store.hpp"
#include "server/userscript_updater.hpp"

namespace sbc::server {

// App is the top-level request router and lifecycle owner. It holds the active
// config + provider behind a snapshot and serves /api, the homepage shell,
// static assets, socket.io, the remote loader, and delegates everything else to
// the active provider.
class App {
public:
    App(config::Store& store, config::Config cfg, host::ProviderContext ctx);

    Handler handler();

    boost::asio::awaitable<void> serve(Request& req, ResponseWriter& raw);

    void close();

    std::string active_address() const { return active_address_; }

    // Native (in-process) RPC access, used by the Android JNI bridge to drive the
    // same dispatcher the /rpc WebSocket uses, without the localhost socket hop.
    // The capability token is verified per inbound frame because the native bridge
    // object is globally visible to page JS (mirroring the WS hello check).
    bool rpc_verify(std::string_view token) const { return rpc_auth_.verify(token); }
    std::shared_ptr<rpc::RpcSession> make_rpc_session(boost::asio::any_io_executor exec,
                                                      rpc::RpcSession::Sender send) {
        return std::make_shared<rpc::RpcSession>(std::move(exec), rpc_dispatcher_, std::move(send));
    }

private:
    struct State {
        config::Config cfg;
        std::shared_ptr<host::Provider> provider;
    };

    std::shared_ptr<const State> snapshot() const;

    // Capability-gated RPC over a single multiplexed WebSocket. handle_rpc accepts
    // the upgrade and drives an RpcConnection; register_rpc_methods wires the method
    // table once (in the constructor). All API surface is served through this.
    boost::asio::awaitable<void> handle_rpc(Request& req, ResponseWriter& w);
    void register_rpc_methods();

    // RPC method bodies that return their response JSON (the dispatcher serializes
    // it into the response frame; errors are raised as rpc::RpcError). The cache
    // helpers below (cache_stats/clear_cache/...) are reused directly.
    boost::asio::awaitable<nlohmann::ordered_json> rpc_config_get();
    boost::asio::awaitable<nlohmann::ordered_json> rpc_config_replace(
        const nlohmann::ordered_json& body);
    // Params are taken by value: this is a coroutine, and the dispatcher binds the
    // arguments to temporaries (p.value("scope") and the slice ternary) that are
    // destroyed at the end of the call expression — before the lazy coroutine body
    // runs. By-value params copy them into the frame while they are still alive.
    boost::asio::awaitable<nlohmann::ordered_json> rpc_config_update_scope(
        std::string scope_name, nlohmann::ordered_json slice, bool migrate);
    boost::asio::awaitable<nlohmann::ordered_json> rpc_config_reset();
    boost::asio::awaitable<nlohmann::ordered_json> rpc_homepage(bool force_revalidate);
    // rpc_stats_event produces one per-store + total cache-stats snapshot; it backs
    // both the cache.stats unary method and the stats.subscribe stream.
    boost::asio::awaitable<nlohmann::ordered_json> rpc_stats_event();

    // Userscript manager methods (dedicated LevelDB store, not a config scope). All
    // store calls bridge onto the blocking pool via net::run_blocking.
    boost::asio::awaitable<nlohmann::ordered_json> rpc_userscript(
        const std::string& method, const nlohmann::ordered_json& params);

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
                                                   const std::string& only_scope,
                                                   bool migrate_cache);

    config::Store& store_;
    host::ProviderContext ctx_;
    std::string active_address_;
    std::shared_ptr<AssetSource> assets_;
    std::vector<ConfigScope> scopes_;

    // game_ is the embedded local game server. It is a direct member (outside
    // State) so toggling localGameServer at runtime never tears it down.
    std::shared_ptr<gameserver::GameApp> game_;

    // userscripts_ holds all userscript state in a dedicated LevelDB store;
    // userscript_updater_ is its background auto-update *checker* (never applies).
    // Both are direct members, independent of the active provider/config.
    std::shared_ptr<UserscriptStore> userscripts_;
    std::unique_ptr<UserscriptUpdater> userscript_updater_;

    // RPC capability secret + method table. Both are process-lifetime and
    // independent of the active config/provider.
    rpc::RpcAuth rpc_auth_;
    rpc::RpcDispatcher rpc_dispatcher_;

    mutable std::shared_mutex state_mu_;
    std::shared_ptr<const State> state_;
};

}  // namespace sbc::server
