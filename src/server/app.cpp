#include "server/app.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include "common/error.hpp"
#include "common/url.hpp"
#include "config/json.hpp"
#include "host/packagehost/provider.hpp"
#include "host/reverseproxy/provider.hpp"
#include "net/blocking_pool.hpp"
#include "net/io_runtime.hpp"
#include "server/api_util.hpp"
#include "server/gameserver/game_socket_local.hpp"
#include "server/homepage.hpp"
#include "server/remote_proxy.hpp"

namespace sbc::server {

namespace asio = boost::asio;
using nlohmann::ordered_json;

namespace {

constexpr std::size_t kMaxConfigBody = 1u << 20;  // 1 MiB, matches Go MaxBytesReader

// CommonHeaderWriter injects the per-response common headers (CORS,
// X-Studio-Local-Host) into every response, mirroring Go's addCommonHeaders.
class CommonHeaderWriter : public ResponseWriter {
public:
    CommonHeaderWriter(ResponseWriter& inner, HeaderMap common)
        : inner_(inner), common_(std::move(common)) {}

    asio::awaitable<void> write_full(int status, HeaderMap headers, std::string body) override {
        merge(headers);
        return inner_.write_full(status, std::move(headers), std::move(body));
    }
    asio::awaitable<void> send_header(int status, HeaderMap headers,
                                      std::optional<std::int64_t> content_length) override {
        merge(headers);
        return inner_.send_header(status, std::move(headers), content_length);
    }
    asio::awaitable<void> write_chunk(std::string_view data) override {
        return inner_.write_chunk(data);
    }
    asio::awaitable<void> finish() override { return inner_.finish(); }
    net::HijackedConnection hijack() override { return inner_.hijack(); }

private:
    void merge(HeaderMap& h) {
        for (const auto& e : common_.entries()) h.set_if_absent(e.first, e.second);
    }
    ResponseWriter& inner_;
    HeaderMap common_;
};

HeaderMap build_common_headers(const Request& req) {
    HeaderMap h;
    std::string origin = req.headers.get("Origin");
    if (origin.rfind("http://localhost:", 0) == 0 || origin.rfind("http://127.0.0.1:", 0) == 0) {
        h.set("Access-Control-Allow-Origin", origin);
        h.set("Access-Control-Allow-Methods", "GET,POST,PUT,OPTIONS");
        h.set("Access-Control-Allow-Headers", "Content-Type");
    }
    h.set("X-Studio-Local-Host", "studio-bondage-club");
    return h;
}

ordered_json stats_json(const cache::Stats& s) {
    ordered_json j;
    j["entries"] = s.entries;
    j["bytes"] = s.bytes;
    return j;
}

// query_value returns the first value of query parameter `name` from a raw query
// string ("a=1&b=2"), or "" if absent.
std::string query_value(const std::string& raw_query, const std::string& name) {
    std::size_t pos = 0;
    while (pos <= raw_query.size()) {
        std::size_t amp = raw_query.find('&', pos);
        std::string pair = raw_query.substr(
            pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::size_t eq = pair.find('=');
        std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
        if (key == name) return eq == std::string::npos ? std::string() : pair.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::string();
}

ordered_json status_json(const host::RuntimeStatus& s) {
    ordered_json j;
    j["mode"] = s.mode;
    if (!s.upstream.empty()) j["upstream"] = s.upstream;
    if (!s.cache_dir.empty()) j["cacheDir"] = s.cache_dir;
    ordered_json caps = ordered_json::array();
    for (const auto& c : s.capabilities) {
        ordered_json cj;
        cj["id"] = c.id;
        cj["label"] = c.label;
        cj["enabled"] = c.enabled;
        cj["description"] = c.description;
        caps.push_back(cj);
    }
    j["capabilities"] = caps;
    return j;
}

ordered_json modes_json() {
    ordered_json arr = ordered_json::array();
    ordered_json a;
    a["id"] = config::kModeReverseProxy;
    a["label"] = "Mode A: reverse proxy cache";
    a["enabled"] = true;
    a["description"] =
        "Proxy the remote Bondage Club server and keep verified local HTTP cache entries.";
    arr.push_back(a);
    ordered_json b;
    b["id"] = config::kModePackage;
    b["label"] = "Mode B: package cache";
    b["enabled"] = false;
    b["description"] =
        "Reserved for local plugin/package bundles and binary diff update workflows.";
    arr.push_back(b);
    return arr;
}

// migrate_dir moves a data tree from old_dir to new_dir. Used when the user opts
// to migrate on a cache.dir or game-storage-path change. No-op if old_dir is
// missing; merges into an existing new_dir.
void migrate_dir(const std::string& old_dir, const std::string& new_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (old_dir.empty() || new_dir.empty() || old_dir == new_dir) return;
    if (!fs::exists(old_dir, ec)) return;
    fs::create_directories(fs::path(new_dir).parent_path(), ec);
    fs::rename(old_dir, new_dir, ec);
    if (!ec) return;
    // Cross-device or non-empty target: fall back to a recursive copy + remove.
    fs::create_directories(new_dir, ec);
    fs::copy(old_dir, new_dir,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) throw Error("migrate dir " + old_dir + " -> " + new_dir + ": " + ec.message());
    fs::remove_all(old_dir, ec);
}

bool store_topology_changed(const std::vector<config::StoreConfig>& old_stores,
                            const std::vector<config::StoreConfig>& new_stores) {
    if (old_stores.size() != new_stores.size()) return true;
    std::map<std::string, std::string> old_map;
    for (const auto& s : old_stores) old_map[s.name] = s.dir;
    for (const auto& s : new_stores) {
        auto it = old_map.find(s.name);
        if (it == old_map.end() || it->second != s.dir) return true;
        old_map.erase(it);
    }
    return !old_map.empty();
}

// strip_route_prefix removes a leading path segment (e.g. "/proxy") from both the
// decoded path and the raw request-target, so the downstream socket handler sees
// a plain "/socket.io/..." request regardless of which endpoint the client chose.
void strip_route_prefix(Request& req, std::string_view prefix) {
    if (req.path.rfind(prefix, 0) == 0) req.path.erase(0, prefix.size());
    if (req.target.rfind(prefix, 0) == 0) req.target.erase(0, prefix.size());
}

}  // namespace

App::App(config::Store& store, config::Config cfg, host::ProviderContext ctx)
    : store_(store), ctx_(std::move(ctx)) {
    active_address_ = cfg.server.address();
    assets_ = default_asset_source();
    std::string game_dir = config::game_storage_dir(cfg);
    game_ = std::make_shared<gameserver::GameApp>(ctx_.io->executor(), *ctx_.blocking, game_dir,
                                                  cfg.game_server_settings);
    auto provider = provider_for(cfg, ctx_);
    state_ = std::make_shared<State>(State{std::move(cfg), std::move(provider)});
    register_scopes();
}

Handler App::handler() {
    return [this](Request& req, ResponseWriter& w) -> asio::awaitable<void> {
        co_await serve(req, w);
    };
}

std::shared_ptr<const App::State> App::snapshot() const {
    std::shared_lock lock(state_mu_);
    return state_;
}

std::shared_ptr<host::Provider> App::provider_for(const config::Config& cfg,
                                                  const host::ProviderContext& ctx) {
    if (cfg.mode == config::kModeReverseProxy) {
        return host::reverseproxy::Provider::create(cfg, ctx);
    }
    if (cfg.mode == config::kModePackage) {
        return host::packagehost::Provider::create(cfg, ctx);
    }
    throw Error("unsupported mode \"" + cfg.mode + "\"");
}

asio::awaitable<void> App::serve(Request& req, ResponseWriter& raw) {
    CommonHeaderWriter w(raw, build_common_headers(req));

    if (req.is_options()) {
        co_await w.write_full(204, HeaderMap{}, "");
        co_return;
    }

    auto state = snapshot();
    const config::Config& cfg = state->cfg;

    if (req.path.rfind("/api/", 0) == 0) {
        co_await handle_api(req, w, *state);
        co_return;
    }
    if (req.path == kServiceWorkerPath) {
        co_await serve_service_worker(req, w, *assets_);
        co_return;
    }
    // Two stable game-socket endpoints, selected by the client (the page wraps
    // socket.io's `path` option — see web/src/originalPage.ts). The frontend owns
    // the local/remote switch, so it can force a reconnect onto the other endpoint
    // cleanly instead of having a live engine.io session rerouted under it.
    if (req.path.rfind("/local/socket.io/", 0) == 0) {
        strip_route_prefix(req, "/local");
        co_await serve_game_socket_local(req, w, game_->hub(), ctx_.io->executor());
        co_return;
    }
    if (req.path.rfind("/proxy/socket.io/", 0) == 0) {
        strip_route_prefix(req, "/proxy");
        co_await serve_game_socket(req, w, cfg.game_server, cfg.upstream, *ctx_.tls,
                                   ctx_.io->executor());
        co_return;
    }
    // Legacy/fallback: an unprefixed /socket.io/ (e.g. if the io wrapper failed to
    // install) is routed by the server-side default flag.
    if (req.path.rfind("/socket.io/", 0) == 0) {
        if (cfg.local_game_server) {
            co_await serve_game_socket_local(req, w, game_->hub(), ctx_.io->executor());
        } else {
            co_await serve_game_socket(req, w, cfg.game_server, cfg.upstream, *ctx_.tls,
                                       ctx_.io->executor());
        }
        co_return;
    }
    if (auto target = remote_loader_target(req)) {
        auto* rp = dynamic_cast<host::RemoteProxyProvider*>(state->provider.get());
        if (rp == nullptr) {
            co_await write_error(w, 501, "remote proxy is not available in the active mode");
        } else {
            co_await rp->serve_remote_http(req, w, *target);
        }
        co_return;
    }
    if (req.path == "/" && (req.is_get() || req.is_head())) {
        co_await serve_homepage_shell(req, w, cfg.upstream, cfg.server.admin_base_path,
                                      cfg.local_game_server);
        co_return;
    }
    std::string admin_no_slash = cfg.server.admin_base_path;
    if (!admin_no_slash.empty() && admin_no_slash.back() == '/') admin_no_slash.pop_back();
    if (req.path == admin_no_slash) {
        co_await not_found(w);
        co_return;
    }
    if (req.path.rfind(cfg.server.admin_base_path, 0) == 0) {
        co_await serve_web_asset(req, w, cfg.server.admin_base_path, *assets_);
        co_return;
    }

    co_await state->provider->serve(req, w);
}

asio::awaitable<void> App::handle_api(Request& req, ResponseWriter& w, const State& state) {
    (void)state;
    const std::string& path = req.path;
    if (path == "/api/health") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        ordered_json j;
        j["ok"] = true;
        co_await write_json(w, 200, j);
    } else if (path == "/api/config") {
        if (req.is_get()) {
            co_await handle_get_config(w);
        } else if (req.method == "PUT") {
            co_await handle_put_config(req, w);
        } else {
            co_await method_not_allowed(w);
        }
    } else if (path.rfind("/api/config/", 0) == 0) {
        if (req.method == "PUT") {
            co_await handle_put_config_scope(req, w, path.substr(std::string("/api/config/").size()));
        } else {
            co_await method_not_allowed(w);
        }
    } else if (path == "/api/modes") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        co_await write_json(w, 200, modes_json());
    } else if (path == "/api/cache/stats") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        cache::Stats stats = co_await cache_stats();
        co_await write_json(w, 200, stats_json(stats));
    } else if (path == "/api/cache/clear") {
        if (req.method != "POST") {
            co_await method_not_allowed(w);
            co_return;
        }
        co_await clear_cache();
        ordered_json j;
        j["ok"] = true;
        co_await write_json(w, 200, j);
    } else if (path == "/api/cache/expire") {
        if (req.method != "POST") {
            co_await method_not_allowed(w);
            co_return;
        }
        std::string store, host, path_prefix, version;
        std::string parse_error;
        if (!req.body.empty()) {
            try {
                ordered_json body = ordered_json::parse(req.body);
                store = body.value("store", "");
                host = body.value("host", "");
                path_prefix = body.value("pathPrefix", "");
                version = body.value("version", "");
            } catch (const std::exception& e) {
                parse_error = e.what();
            }
        }
        if (!parse_error.empty()) {
            co_await write_error(w, 400, std::string("decode body: ") + parse_error);
            co_return;
        }
        int expired = co_await expire_cache(store, host, path_prefix, version);
        ordered_json j;
        j["ok"] = true;
        j["expired"] = expired;
        co_await write_json(w, 200, j);
    } else if (path == "/api/cache/versions") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        std::string store = query_value(req.raw_query, "store");
        auto versions = co_await cache_versions(store);
        ordered_json arr = ordered_json::array();
        for (const auto& [ver, count] : versions) {
            ordered_json e;
            e["version"] = ver;
            e["entries"] = count;
            arr.push_back(std::move(e));
        }
        co_await write_json(w, 200, arr);
    } else if (path == "/api/events") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        co_await handle_sse(w);
    } else if (path == "/api/homepage") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        co_await handle_get_homepage(req, w);
    } else if (path == "/api/gameserver/status") {
        if (!req.is_get()) {
            co_await method_not_allowed(w);
            co_return;
        }
        ordered_json j;
        j["enabled"] = state.cfg.local_game_server;
        j["online"] = game_ ? static_cast<std::int64_t>(game_->accounts().online_count()) : 0;
        j["rooms"] = game_ ? static_cast<std::int64_t>(game_->chatrooms().room_count()) : 0;
        co_await write_json(w, 200, j);
    } else {
        co_await not_found(w);
    }
}

asio::awaitable<void> App::handle_get_config(ResponseWriter& w) {
    auto state = snapshot();
    cache::Stats stats = co_await cache_stats();
    ordered_json resp;
    resp["config"] = state->cfg;
    resp["status"] = status_json(state->provider->status());
    resp["cache"] = stats_json(stats);
    resp["configPath"] = store_.path().string();
    resp["restartRequired"] = state->cfg.server.address() != active_address_;
    co_await write_json(w, 200, resp);
}

asio::awaitable<void> App::handle_put_config(Request& req, ResponseWriter& w) {
    if (req.body.size() > kMaxConfigBody) {
        co_await write_error(w, 400, "decode config: request body too large");
        co_return;
    }

    config::Config new_cfg;
    std::string error_msg;
    bool failed = false;
    try {
        new_cfg = config::normalize(config::parse_strict(req.body));
    } catch (const std::exception& e) {
        error_msg = std::string("decode config: ") + e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 400, error_msg);
        co_return;
    }

    try {
        new_cfg.validate();
    } catch (const std::exception& e) {
        error_msg = e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 400, error_msg);
        co_return;
    }

    std::map<std::string, UpdateTier> changed;
    try {
        changed = apply_config(new_cfg, /*only_scope=*/"", /*migrate_cache=*/false);
    } catch (const std::exception& e) {
        error_msg = e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 500, error_msg);
        co_return;
    }

    // Aggregate tier for the legacy whole-config response = the strongest tier.
    UpdateTier tier = UpdateTier::Live;
    for (const auto& [name, t] : changed)
        if (static_cast<int>(t) > static_cast<int>(tier)) tier = t;

    auto updated = snapshot();
    cache::Stats stats = co_await cache_stats();
    ordered_json resp;
    resp["config"] = new_cfg;
    resp["status"] = status_json(updated->provider->status());
    resp["cache"] = stats_json(stats);
    resp["configPath"] = store_.path().string();
    resp["restartRequired"] = new_cfg.server.address() != active_address_;
    if (tier != UpdateTier::Live) resp["updateTier"] = static_cast<int>(tier);
    co_await write_json(w, 200, resp);
}

asio::awaitable<void> App::handle_put_config_scope(Request& req, ResponseWriter& w,
                                                   const std::string& scope_name) {
    const ConfigScope* scope = find_scope(scope_name);
    if (scope == nullptr) {
        co_await not_found(w);
        co_return;
    }
    if (req.body.size() > kMaxConfigBody) {
        co_await write_error(w, 400, "decode config: request body too large");
        co_return;
    }

    std::string error_msg;
    bool failed = false;

    // Merge the submitted slice into a copy of the live config, then validate the
    // whole config so cross-field invariants still hold.
    config::Config merged = snapshot()->cfg;
    try {
        ordered_json slice = ordered_json::parse(req.body);
        if (!slice.is_object()) throw Error("scope body must be a JSON object");
        scope->set(merged, slice);
        merged = config::normalize(std::move(merged));
        merged.validate();
    } catch (const std::exception& e) {
        error_msg = std::string("decode config: ") + e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 400, error_msg);
        co_return;
    }

    // Cache-dir migration is opt-in via ?migrate=true (only meaningful for cache).
    const bool migrate = req.raw_query.find("migrate=true") != std::string::npos;

    std::map<std::string, UpdateTier> changed;
    try {
        changed = apply_config(merged, scope_name, migrate);
    } catch (const std::exception& e) {
        error_msg = e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 500, error_msg);
        co_return;
    }

    auto it = changed.find(scope_name);
    UpdateTier tier = it == changed.end() ? UpdateTier::Live : it->second;

    auto updated = snapshot();
    ordered_json resp;
    resp["scope"] = scope_name;
    resp["slice"] = scope->get(updated->cfg);
    resp["updateTier"] = static_cast<int>(tier);
    resp["restartRequired"] = updated->cfg.server.address() != active_address_;
    resp["configPath"] = store_.path().string();
    co_await write_json(w, 200, resp);
}

void App::register_scopes() {
    using cfg_t = config::Config;
    scopes_.push_back(ConfigScope{
        "connection",
        [](const cfg_t& c) {
            ordered_json j;
            j["host"] = c.server.host;
            j["port"] = c.server.port;
            j["adminBasePath"] = c.server.admin_base_path;
            j["upstream"] = c.upstream;
            j["socks5Proxy"] = c.socks5_proxy;
            j["gameServer"] = c.game_server;
            return j;
        },
        [](cfg_t& c, const ordered_json& j) {
            if (auto it = j.find("host"); it != j.end()) c.server.host = it->get<std::string>();
            if (auto it = j.find("port"); it != j.end()) c.server.port = it->get<int>();
            if (auto it = j.find("adminBasePath"); it != j.end())
                c.server.admin_base_path = it->get<std::string>();
            if (auto it = j.find("upstream"); it != j.end()) c.upstream = it->get<std::string>();
            if (auto it = j.find("socks5Proxy"); it != j.end())
                c.socks5_proxy = it->get<std::string>();
            if (auto it = j.find("gameServer"); it != j.end()) c.game_server = it->get<std::string>();
        },
        [](const cfg_t& o, const cfg_t& n) {
            return (o.server.host != n.server.host || o.server.port != n.server.port)
                       ? UpdateTier::Restart
                       : UpdateTier::Live;
        }});
    scopes_.push_back(ConfigScope{
        "cache", [](const cfg_t& c) { return ordered_json(c.cache); },
        [](cfg_t& c, const ordered_json& j) { config::from_json(j, c.cache); },
        [](const cfg_t& o, const cfg_t& n) {
            return (o.cache.dir != n.cache.dir ||
                    store_topology_changed(o.cache.stores, n.cache.stores))
                       ? UpdateTier::Recreate
                       : UpdateTier::Live;
        }});
    // gameserver: the local/remote routing toggle (read per request -> live) and
    // the embedded game DB storage path (hot-reloaded/migrated by apply_config,
    // independent of the cache provider, so it stays a Live tier).
    scopes_.push_back(ConfigScope{
        "gameserver",
        [](const cfg_t& c) {
            return ordered_json{{"localGameServer", c.local_game_server},
                                {"gameServerStoragePath", c.game_server_storage_path}};
        },
        [](cfg_t& c, const ordered_json& j) {
            if (auto it = j.find("localGameServer"); it != j.end())
                c.local_game_server = it->get<bool>();
            if (auto it = j.find("gameServerStoragePath"); it != j.end())
                c.game_server_storage_path = it->get<std::string>();
        },
        [](const cfg_t&, const cfg_t&) { return UpdateTier::Live; }});
    scopes_.push_back(ConfigScope{
        "gamesettings", [](const cfg_t& c) { return ordered_json(c.game_server_settings); },
        [](cfg_t& c, const ordered_json& j) { config::from_json(j, c.game_server_settings); },
        [](const cfg_t&, const cfg_t&) { return UpdateTier::Live; }});
    scopes_.push_back(ConfigScope{
        "mode", [](const cfg_t& c) { return ordered_json{{"mode", c.mode}}; },
        [](cfg_t& c, const ordered_json& j) {
            if (auto it = j.find("mode"); it != j.end()) c.mode = it->get<std::string>();
        },
        [](const cfg_t&, const cfg_t&) { return UpdateTier::Recreate; }});
    scopes_.push_back(ConfigScope{
        "package", [](const cfg_t& c) { return ordered_json(c.package); },
        [](cfg_t& c, const ordered_json& j) { config::from_json(j, c.package); },
        [](const cfg_t&, const cfg_t&) { return UpdateTier::Live; }});
}

const ConfigScope* App::find_scope(const std::string& name) const {
    for (const auto& s : scopes_)
        if (s.name == name) return &s;
    return nullptr;
}

std::map<std::string, UpdateTier> App::apply_config(const config::Config& new_cfg,
                                                    const std::string& only_scope,
                                                    bool migrate_cache) {
    config::Config old_cfg = snapshot()->cfg;

    std::map<std::string, UpdateTier> changed;
    bool needs_recreate = false;
    bool needs_live = false;
    bool game_settings_changed = false;
    for (const auto& sc : scopes_) {
        if (!only_scope.empty() && sc.name != only_scope) continue;
        if (!sc.changed(old_cfg, new_cfg)) continue;
        UpdateTier t = sc.tier(old_cfg, new_cfg);
        changed[sc.name] = t;
        if (t == UpdateTier::Recreate) needs_recreate = true;
        if (sc.name == "gamesettings") {
            game_settings_changed = true;
        } else if ((sc.name == "connection" || sc.name == "cache" || sc.name == "package") &&
                   t == UpdateTier::Live) {
            needs_live = true;
        }
    }
    if (changed.empty()) return changed;

    // Provider work happens only when a provider-affecting scope changed; a pure
    // gamesettings/gameserver/connection-address change never touches the provider
    // (so its in-flight cache requests are undisturbed).
    std::shared_ptr<host::Provider> provider = snapshot()->provider;
    const bool cache_dir_changed = old_cfg.cache.dir != new_cfg.cache.dir;

    // The embedded game DB directory may move either because the explicit
    // gameServerStoragePath changed, or because it defaults to <cache.dir>/
    // gameserver and the cache dir moved. This is independent of the cache
    // provider, so it is hot-migrated here even when no provider rebuild happens.
    const std::string old_game_dir = config::game_storage_dir(old_cfg);
    const std::string new_game_dir = config::game_storage_dir(new_cfg);
    const bool game_dir_changed = old_game_dir != new_game_dir;
    // When both old and new leave the path at its default, the game DB lives
    // inside the cache tree, so the cache-dir migrate physically relocates it for
    // us — don't move it a second time.
    const bool game_move_covered_by_cache = old_cfg.game_server_storage_path.empty() &&
                                            new_cfg.game_server_storage_path.empty() &&
                                            cache_dir_changed;

    // Quiesce the game DB before any physical move of its data directory.
    if (game_ && game_dir_changed) game_->detach_db();
    // Dedicated relocation when the move isn't carried by the cache-tree migrate.
    // Runs before the cache migrate so a nested default dir isn't moved twice.
    if (migrate_cache && game_dir_changed && !game_move_covered_by_cache)
        migrate_dir(old_game_dir, new_game_dir);

    if (needs_recreate) {
        if (migrate_cache && cache_dir_changed) migrate_dir(old_cfg.cache.dir, new_cfg.cache.dir);

        std::shared_ptr<host::Provider> old_provider;
        {
            std::unique_lock lock(state_mu_);
            old_provider = state_->provider;
        }
        if (old_provider) old_provider->close();
        std::shared_ptr<host::Provider> new_provider;
        try {
            new_provider = provider_for(new_cfg, ctx_);
        } catch (...) {
            try {
                auto restored = provider_for(old_cfg, ctx_);
                std::unique_lock lock(state_mu_);
                state_ = std::make_shared<State>(State{old_cfg, restored});
            } catch (...) {
            }
            // Re-attach the game DB so it isn't left detached. If we already moved
            // its data, it now lives at the new path; otherwise it's untouched.
            if (game_ && game_dir_changed)
                game_->reopen(migrate_cache ? new_game_dir : old_game_dir);
            throw;
        }
        provider = new_provider;
    } else if (needs_live) {
        if (auto* lu = dynamic_cast<host::LiveUpdater*>(provider.get())) lu->live_update(new_cfg);
    }

    // Reopen the game DB at its (possibly migrated) new location. With migrate off
    // the new directory is a fresh, empty store.
    if (game_ && game_dir_changed) game_->reopen(new_game_dir);

    {
        std::unique_lock lock(state_mu_);
        state_ = std::make_shared<State>(State{new_cfg, provider});
    }

    // Game settings: pure COW swap, no provider involvement, zero teardown.
    if (game_settings_changed && game_) game_->update_settings(new_cfg.game_server_settings);

    store_.save(new_cfg);
    return changed;
}

asio::awaitable<cache::Stats> App::cache_stats() {
    auto state = snapshot();
    cache::Stats total;
    auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get());
    if (sp == nullptr) co_return total;
    for (auto& store : sp->all_stores()) {
        cache::Stats s =
            co_await net::run_blocking(*ctx_.blocking, [store]() { return store->stats(); });
        total.entries += s.entries;
        total.bytes += s.bytes;
    }
    co_return total;
}

asio::awaitable<void> App::clear_cache() {
    auto state = snapshot();
    auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get());
    if (sp == nullptr) co_return;
    for (auto& store : sp->all_stores()) {
        co_await net::run_blocking(*ctx_.blocking, [store]() { store->clear(); });
    }
}

asio::awaitable<int> App::expire_cache(const std::string& store, const std::string& host,
                                       const std::string& path_prefix, const std::string& version) {
    auto state = snapshot();
    auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get());
    if (sp == nullptr) co_return 0;
    auto now = cache::Clock::now();
    auto match = [host, path_prefix, version](const cache::Metadata& m) {
        if (!version.empty() && m.version != version) return false;
        if (!host.empty() || !path_prefix.empty()) {
            auto u = Url::try_parse(m.url);
            if (!u) return false;
            if (!host.empty() && u->host() != host) return false;
            if (!path_prefix.empty() && u->path().rfind(path_prefix, 0) != 0) return false;
        }
        return true;
    };
    int total = 0;
    for (auto& s : sp->all_stores()) {
        if (!store.empty() && s->name() != store) continue;
        total += co_await net::run_blocking(*ctx_.blocking,
                                            [s, &match, now]() { return s->expire(match, now); });
    }
    co_return total;
}

asio::awaitable<std::vector<std::pair<std::string, int>>> App::cache_versions(
    const std::string& store) {
    auto state = snapshot();
    std::map<std::string, int> merged;
    auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get());
    if (sp == nullptr) co_return std::vector<std::pair<std::string, int>>{};
    for (auto& s : sp->all_stores()) {
        if (!store.empty() && s->name() != store) continue;
        auto vs = co_await net::run_blocking(*ctx_.blocking, [s]() { return s->versions(); });
        for (const auto& [ver, count] : vs) merged[ver] += count;
    }
    co_return std::vector<std::pair<std::string, int>>{merged.begin(), merged.end()};
}

asio::awaitable<void> App::handle_sse(ResponseWriter& w) {
    HeaderMap headers;
    headers.set("Content-Type", "text/event-stream");
    headers.set("Cache-Control", "no-cache");
    headers.set("X-Accel-Buffering", "no");
    co_await w.send_header(200, std::move(headers), std::nullopt);

    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);

    for (;;) {
        auto state = snapshot();
        ordered_json evt;
        evt["type"] = "stats";
        ordered_json stores = ordered_json::array();
        cache::Stats total;
        if (auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get())) {
            for (auto& store : sp->all_stores()) {
                cache::Stats s = co_await net::run_blocking(*ctx_.blocking,
                                                            [store]() { return store->stats(); });
                ordered_json sj;
                sj["name"] = store->name();
                sj["stats"] = stats_json(s);
                stores.push_back(sj);
                total.entries += s.entries;
                total.bytes += s.bytes;
            }
        }
        evt["stores"] = stores;
        evt["total"] = stats_json(total);

        std::string frame = "data: " + evt.dump() + "\n\n";
        co_await w.write_chunk(frame);

        timer.expires_after(std::chrono::seconds(2));
        co_await timer.async_wait(asio::use_awaitable);
    }
}

asio::awaitable<void> App::handle_get_homepage(Request& req, ResponseWriter& w) {
    auto state = snapshot();
    auto* hp = dynamic_cast<host::HomepageProvider*>(state->provider.get());
    if (hp == nullptr) {
        co_await write_error(w, 501, "homepage source is not available in the active mode");
        co_return;
    }
    host::HomepageDocument doc;
    std::string error_msg;
    bool failed = false;
    try {
        doc = co_await hp->fetch_homepage(req);
    } catch (const std::exception& e) {
        error_msg = std::string("fetch homepage source: ") + e.what();
        failed = true;
    }
    if (failed) {
        co_await write_error(w, 502, error_msg);
        co_return;
    }
    ordered_json j;
    j["html"] = doc.body;
    j["url"] = doc.url;
    j["statusCode"] = doc.status_code;
    j["cacheStatus"] = doc.cache_status;
    HeaderMap headers;
    headers.set("Content-Type", "application/json; charset=utf-8");
    headers.set("Cache-Control", "no-store");
    co_await w.write_full(200, std::move(headers), j.dump());
}

void App::close() {
    if (game_) game_->close();
    auto state = snapshot();
    if (state && state->provider) state->provider->close();
}

}  // namespace sbc::server
