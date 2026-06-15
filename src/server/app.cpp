#include "server/app.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <nlohmann/json.hpp>

#include "common/error.hpp"
#include "common/http_util.hpp"
#include "common/url.hpp"
#include "config/json.hpp"
#include "host/packagehost/provider.hpp"
#include "host/reverseproxy/provider.hpp"
#include "net/blocking_pool.hpp"
#include "net/io_runtime.hpp"
#include "net/tls.hpp"
#include "server/api_util.hpp"
#include "server/gameserver/game_socket_local.hpp"
#include "server/homepage.hpp"
#include "server/remote_proxy.hpp"
#include "server/rpc/connection.hpp"
#include "server/userscript_defaults.hpp"

namespace sbc::server {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using nlohmann::ordered_json;

namespace {

constexpr std::size_t kMaxConfigBody = 1u << 20;  // 1 MiB

// rebuild_request reconstructs a Beast request from the internal Request so the
// WebSocket handshake can complete on the hijacked socket. Mirrors the socket.io
// server's helper.
http::request<http::empty_body> rebuild_request(const Request& req) {
    http::request<http::empty_body> out;
    out.method(http::verb::get);
    out.target(req.target);
    out.version(11);
    for (const auto& e : req.headers.entries()) out.insert(e.first, e.second);
    return out;
}

// CommonHeaderWriter injects the shared response headers (CORS,
// X-Studio-Local-Host) without overwriting handler-provided values.
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

// percent_decode decodes %XX escapes and '+'-as-space in a query component, so
// arbitrary GM value keys (which the frontend encodes with encodeURIComponent)
// round-trip through ?key=K.
std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]);
            int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// query_value returns the first value of query parameter `name` from a raw query
// string ("a=1&b=2"), or "" if absent.
std::string query_value(const std::string& raw_query, const std::string& name) {
    std::size_t pos = 0;
    while (pos <= raw_query.size()) {
        std::size_t amp = raw_query.find('&', pos);
        std::string pair =
            raw_query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
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
    fs::copy(old_dir, new_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
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
    userscripts_ = UserscriptStore::open(config::userscript_storage_dir(cfg));
    for (const auto& spec : builtin_userscripts()) userscripts_->ensure_builtin(spec);
    auto provider = provider_for(cfg, ctx_);
    state_ = std::make_shared<State>(State{std::move(cfg), std::move(provider)});
    register_scopes();
    register_rpc_methods();

    // The updater reads the live config (for socks5) via snapshot(); start it
    // only once state_ is set. It records pending updates only — never applies.
    userscript_updater_ =
        std::make_unique<UserscriptUpdater>(ctx_.io->executor(), *ctx_.blocking, *ctx_.tls,
                                            userscripts_, [this]() { return snapshot()->cfg; });
    userscript_updater_->start();
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

    if (req.path == "/rpc") {
        co_await handle_rpc(req, w);
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
    // Fallback for an unprefixed /socket.io/ request if the frontend wrapper is
    // not installed.
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
                                      cfg.local_game_server, rpc_auth_.token());
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

asio::awaitable<void> App::handle_rpc(Request& req, ResponseWriter& w) {
    // The capability boundary is enforced inside the connection's hello handshake
    // (RpcAuth); the upgrade itself is open, like any same-origin endpoint.
    const bool is_upgrade = sbc::iequals(req.headers.get("Upgrade"), "websocket") &&
                            sbc::header_contains_token(req.headers.get("Connection"), "upgrade");
    if (!is_upgrade) {
        co_await write_error(w, 400, "the /rpc endpoint requires a WebSocket upgrade");
        co_return;
    }
    net::HijackedConnection hc = w.hijack();
    websocket::stream<beast::tcp_stream> ws(std::move(hc.stream));
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    co_await ws.async_accept(rebuild_request(req), asio::use_awaitable);

    // Drive the connection on its own strand so the concurrent reader/writer and
    // any spawned request/subscription coroutines serialize their stream + state
    // access (same reasoning as the socket.io connection).
    auto strand = asio::make_strand(ctx_.io->executor());
    auto conn =
        std::make_shared<rpc::RpcConnection>(std::move(ws), strand, rpc_dispatcher_, rpc_auth_);
    co_await asio::co_spawn(strand, conn->run(), asio::use_awaitable);
}

void App::register_rpc_methods() {
    using nlohmann::ordered_json;
    auto& d = rpc_dispatcher_;

    // Config — forwarders to the json-returning method bodies.
    d.add("config.get", [this](const ordered_json&) { return rpc_config_get(); });
    d.add("config.replace", [this](const ordered_json& p) { return rpc_config_replace(p); });
    d.add("config.reset", [this](const ordered_json&) { return rpc_config_reset(); });
    d.add("config.updateScope", [this](const ordered_json& p) {
        return rpc_config_update_scope(p.value("scope", std::string{}),
                                       p.contains("slice") ? p["slice"] : ordered_json::object(),
                                       p.value("migrate", false));
    });

    d.add("modes.list",
          [](const ordered_json&) -> asio::awaitable<ordered_json> { co_return modes_json(); });

    // Cache.
    d.add("cache.stats", [this](const ordered_json&) -> asio::awaitable<ordered_json> {
        cache::Stats stats = co_await cache_stats();
        co_return stats_json(stats);
    });
    d.add("cache.clear", [this](const ordered_json&) -> asio::awaitable<ordered_json> {
        co_await clear_cache();
        co_return ordered_json{{"ok", true}};
    });
    d.add("cache.expire", [this](const ordered_json& p) -> asio::awaitable<ordered_json> {
        int expired = co_await expire_cache(p.value("store", ""), p.value("host", ""),
                                            p.value("pathPrefix", ""), p.value("version", ""));
        co_return ordered_json{{"ok", true}, {"expired", expired}};
    });
    d.add("cache.versions", [this](const ordered_json& p) -> asio::awaitable<ordered_json> {
        auto versions = co_await cache_versions(p.value("store", ""));
        ordered_json arr = ordered_json::array();
        for (const auto& [ver, count] : versions)
            arr.push_back(ordered_json{{"version", ver}, {"entries", count}});
        co_return arr;
    });

    d.add("gameserver.status", [this](const ordered_json&) -> asio::awaitable<ordered_json> {
        auto state = snapshot();
        ordered_json j;
        j["enabled"] = state->cfg.local_game_server;
        j["online"] = game_ ? static_cast<std::int64_t>(game_->accounts().online_count()) : 0;
        j["rooms"] = game_ ? static_cast<std::int64_t>(game_->chatrooms().room_count()) : 0;
        co_return j;
    });

    d.add("homepage.get", [this](const ordered_json& p) {
        return rpc_homepage(p.value("forceRevalidate", false));
    });

    // Userscript manager — every method routes through rpc_userscript.
    for (const char* m :
         {"userscripts.list", "userscripts.save", "userscripts.delete", "userscripts.reorder",
          "userscripts.pending", "userscripts.applyUpdate", "userscripts.dismissUpdate",
          "userscripts.values.get", "userscripts.values.set", "userscripts.values.delete",
          "userscripts.settings.get", "userscripts.settings.set", "userscripts.checkUpdates"}) {
        std::string name = m;
        d.add(name, [this, name](const ordered_json& p) { return rpc_userscript(name, p); });
    }

    // Streaming: per-store cache stats every 2s (replaces the SSE /api/events feed).
    d.add_stream(
        "stats.subscribe", [this]() { return rpc_stats_event(); }, std::chrono::seconds(2));
}

asio::awaitable<ordered_json> App::rpc_userscript(const std::string& method,
                                                  const ordered_json& params) {
    auto& bp = *ctx_.blocking;
    auto store = userscripts_;
    // The store speaks nlohmann::json; the dump/parse round-trip bridges it to the
    // ordered_json the RPC layer uses. Payloads here are small, so it's cheap.
    auto as_ordered = [](const nlohmann::json& j) { return ordered_json::parse(j.dump()); };

    if (method == "userscripts.list") {
        nlohmann::json arr = co_await net::run_blocking(bp, [store]() {
            nlohmann::json out = nlohmann::json::array();
            for (auto& s : store->list()) {
                nlohmann::json e = s;
                std::string id = s.value("id", "");
                if (auto p = store->get_pending(id)) {
                    e["pendingUpdate"] = {{"version", p->value("version", "")},
                                          {"fetchedAt", p->value("fetchedAt", std::int64_t{0})}};
                }
                out.push_back(std::move(e));
            }
            return out;
        });
        co_return as_ordered(arr);
    }
    if (method == "userscripts.save") {
        nlohmann::json script = nlohmann::json::parse(params.dump());
        if (!script.is_object() || script.value("id", "").empty())
            throw rpc::RpcError("bad_request", "userscript requires a non-empty id");
        nlohmann::json saved = co_await net::run_blocking(bp, [store, script]() mutable {
            // Built-in defaults: id, name and source URLs are immutable, and the
            // flag can't be forged onto a user script. Everything else is the
            // caller's to change.
            auto existing = store->get(script.value("id", ""));
            if (existing && existing->value("builtin", false)) {
                script["builtin"] = true;
                script["name"] = existing->value("name", script.value("name", ""));
                if (existing->contains("downloadURL"))
                    script["downloadURL"] = (*existing)["downloadURL"];
                else
                    script.erase("downloadURL");
                if (existing->contains("updateURL"))
                    script["updateURL"] = (*existing)["updateURL"];
                else
                    script.erase("updateURL");
            } else {
                script.erase("builtin");
            }
            store->put(script);
            return script;
        });
        co_return as_ordered(saved);
    }
    if (method == "userscripts.delete") {
        std::string id = params.value("script", "");
        if (id.empty()) throw rpc::RpcError("bad_request", "missing script id");
        bool is_builtin = co_await net::run_blocking(bp, [store, id]() {
            auto existing = store->get(id);
            return existing && existing->value("builtin", false);
        });
        if (is_builtin) throw rpc::RpcError("forbidden", "cannot delete a built-in userscript");
        co_await net::run_blocking(bp, [store, id]() { store->remove(id); });
        co_return ordered_json{{"ok", true}};
    }
    if (method == "userscripts.reorder") {
        if (!params.contains("ids") || !params["ids"].is_array())
            throw rpc::RpcError("bad_request", "reorder requires an ids array");
        std::vector<std::string> ids;
        for (const auto& v : params["ids"]) ids.push_back(v.get<std::string>());
        co_await net::run_blocking(bp, [store, ids]() { store->reorder(ids); });
        co_return ordered_json{{"ok", true}};
    }
    if (method == "userscripts.pending") {
        std::string id = params.value("script", "");
        auto pending =
            co_await net::run_blocking(bp, [store, id]() { return store->get_pending(id); });
        if (!pending) throw rpc::RpcError("not_found", "no pending update for script");
        co_return as_ordered(*pending);
    }
    if (method == "userscripts.applyUpdate") {
        std::string id = params.value("script", "");
        auto updated =
            co_await net::run_blocking(bp, [store, id]() { return store->apply_pending(id); });
        if (!updated) throw rpc::RpcError("not_found", "no pending update for script");
        co_return as_ordered(*updated);
    }
    if (method == "userscripts.dismissUpdate") {
        std::string id = params.value("script", "");
        co_await net::run_blocking(bp, [store, id]() { store->clear_pending(id); });
        co_return ordered_json{{"ok", true}};
    }
    if (method == "userscripts.values.get") {
        std::string id = params.value("script", "");
        if (id.empty()) throw rpc::RpcError("bad_request", "missing script id");
        nlohmann::json vals =
            co_await net::run_blocking(bp, [store, id]() { return store->values(id); });
        co_return as_ordered(vals);
    }
    if (method == "userscripts.values.set") {
        std::string id = params.value("script", "");
        std::string key = params.value("key", "");
        if (id.empty()) throw rpc::RpcError("bad_request", "missing script id");
        if (key.empty()) throw rpc::RpcError("bad_request", "missing value key");
        // GM values are stored as raw JSON text, matching the previous PUT body.
        std::string raw = params.contains("value") ? params["value"].dump() : std::string("null");
        try {
            co_await net::run_blocking(bp,
                                       [store, id, key, raw]() { store->set_value(id, key, raw); });
        } catch (const std::exception& e) {
            throw rpc::RpcError("bad_request", std::string("set value: ") + e.what());
        }
        co_return ordered_json{{"ok", true}};
    }
    if (method == "userscripts.values.delete") {
        std::string id = params.value("script", "");
        std::string key = params.value("key", "");
        if (id.empty()) throw rpc::RpcError("bad_request", "missing script id");
        if (key.empty()) throw rpc::RpcError("bad_request", "missing value key");
        co_await net::run_blocking(bp, [store, id, key]() { store->del_value(id, key); });
        co_return ordered_json{{"ok", true}};
    }
    if (method == "userscripts.settings.get") {
        nlohmann::json s =
            co_await net::run_blocking(bp, [store]() { return store->get_settings(); });
        co_return as_ordered(s);
    }
    if (method == "userscripts.settings.set") {
        nlohmann::json settings = nlohmann::json::parse(params.dump());
        if (!settings.is_object())
            throw rpc::RpcError("bad_request", "settings must be a JSON object");
        co_await net::run_blocking(bp, [store, settings]() { store->set_settings(settings); });
        co_return as_ordered(settings);
    }
    if (method == "userscripts.checkUpdates") {
        nlohmann::json summary = co_await userscript_updater_->check_now();
        co_return as_ordered(summary);
    }
    throw rpc::RpcError("method_not_found", "unknown RPC method: " + method);
}

asio::awaitable<ordered_json> App::rpc_config_get() {
    auto state = snapshot();
    cache::Stats stats = co_await cache_stats();
    ordered_json resp;
    resp["config"] = state->cfg;
    resp["status"] = status_json(state->provider->status());
    resp["cache"] = stats_json(stats);
    resp["configPath"] = store_.path().string();
    resp["restartRequired"] = state->cfg.server.address() != active_address_;
    co_return resp;
}

asio::awaitable<ordered_json> App::rpc_config_replace(const ordered_json& body) {
    std::string text = body.dump();
    if (text.size() > kMaxConfigBody)
        throw rpc::RpcError("bad_request", "decode config: request body too large");

    config::Config new_cfg;
    try {
        new_cfg = config::normalize(config::parse_strict(text));
    } catch (const std::exception& e) {
        throw rpc::RpcError("bad_request", std::string("decode config: ") + e.what());
    }
    try {
        new_cfg.validate();
    } catch (const std::exception& e) {
        throw rpc::RpcError("bad_request", e.what());
    }

    std::map<std::string, UpdateTier> changed;
    try {
        changed = apply_config(new_cfg, /*only_scope=*/"", /*migrate_cache=*/false);
    } catch (const std::exception& e) {
        throw rpc::RpcError("internal", e.what());
    }

    // Whole-config responses report the strongest changed-scope tier.
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
    co_return resp;
}

asio::awaitable<ordered_json> App::rpc_config_update_scope(std::string scope_name,
                                                           ordered_json slice, bool migrate) {
    const ConfigScope* scope = find_scope(scope_name);
    if (scope == nullptr) throw rpc::RpcError("not_found", "unknown config scope: " + scope_name);
    if (slice.dump().size() > kMaxConfigBody)
        throw rpc::RpcError("bad_request", "decode config: request body too large");

    // Merge the submitted slice into a copy of the live config, then validate the
    // whole config so cross-field invariants still hold.
    config::Config merged = snapshot()->cfg;
    try {
        if (!slice.is_object()) throw Error("scope body must be a JSON object");
        scope->set(merged, slice);
        merged = config::normalize(std::move(merged));
        merged.validate();
    } catch (const std::exception& e) {
        throw rpc::RpcError("bad_request", std::string("decode config: ") + e.what());
    }

    std::map<std::string, UpdateTier> changed;
    try {
        changed = apply_config(merged, scope_name, migrate);
    } catch (const std::exception& e) {
        throw rpc::RpcError("internal", e.what());
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
    co_return resp;
}

// Restore the whole config to default_config(), persist it, and hot-apply what
// can change live. A listener-address change is reported via restartRequired.
asio::awaitable<ordered_json> App::rpc_config_reset() {
    config::Config def = config::normalize(config::default_config());
    try {
        def.validate();
        apply_config(def, /*only_scope=*/"", /*migrate_cache=*/false);
    } catch (const std::exception& e) {
        throw rpc::RpcError("internal", e.what());
    }

    auto updated = snapshot();
    cache::Stats stats = co_await cache_stats();
    ordered_json resp;
    resp["config"] = updated->cfg;
    resp["status"] = status_json(updated->provider->status());
    resp["cache"] = stats_json(stats);
    resp["configPath"] = store_.path().string();
    resp["restartRequired"] = updated->cfg.server.address() != active_address_;
    co_return resp;
}

void App::register_scopes() {
    using cfg_t = config::Config;
    scopes_.push_back(ConfigScope{"connection",
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
                                      if (auto it = j.find("host"); it != j.end())
                                          c.server.host = it->get<std::string>();
                                      if (auto it = j.find("port"); it != j.end())
                                          c.server.port = it->get<int>();
                                      if (auto it = j.find("adminBasePath"); it != j.end())
                                          c.server.admin_base_path = it->get<std::string>();
                                      if (auto it = j.find("upstream"); it != j.end())
                                          c.upstream = it->get<std::string>();
                                      if (auto it = j.find("socks5Proxy"); it != j.end())
                                          c.socks5_proxy = it->get<std::string>();
                                      if (auto it = j.find("gameServer"); it != j.end())
                                          c.game_server = it->get<std::string>();
                                  },
                                  [](const cfg_t& o, const cfg_t& n) {
                                      return (o.server.host != n.server.host ||
                                              o.server.port != n.server.port)
                                                 ? UpdateTier::Restart
                                                 : UpdateTier::Live;
                                  }});
    scopes_.push_back(
        ConfigScope{"cache", [](const cfg_t& c) { return ordered_json(c.cache); },
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
    scopes_.push_back(ConfigScope{"gameserver",
                                  [](const cfg_t& c) {
                                      return ordered_json{
                                          {"localGameServer", c.local_game_server},
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
    scopes_.push_back(ConfigScope{"mode",
                                  [](const cfg_t& c) { return ordered_json{{"mode", c.mode}}; },
                                  [](cfg_t& c, const ordered_json& j) {
                                      if (auto it = j.find("mode"); it != j.end())
                                          c.mode = it->get<std::string>();
                                  },
                                  [](const cfg_t&, const cfg_t&) { return UpdateTier::Recreate; }});
    scopes_.push_back(
        ConfigScope{"package", [](const cfg_t& c) { return ordered_json(c.package); },
                    [](cfg_t& c, const ordered_json& j) { config::from_json(j, c.package); },
                    [](const cfg_t&, const cfg_t&) { return UpdateTier::Live; }});
#if defined(__ANDROID__)
    // android: GeckoView prefs (hardware acceleration) are read once at runtime
    // startup, so a change can only take effect after an app restart.
    scopes_.push_back(ConfigScope{"android",
                                  [](const cfg_t& c) {
                                      return ordered_json{{"hardwareAcceleration",
                                                           c.android.hardware_acceleration}};
                                  },
                                  [](cfg_t& c, const ordered_json& j) {
                                      if (auto it = j.find("hardwareAcceleration"); it != j.end())
                                          c.android.hardware_acceleration = it->get<bool>();
                                  },
                                  [](const cfg_t&, const cfg_t&) { return UpdateTier::Restart; }});
#endif
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

asio::awaitable<ordered_json> App::rpc_stats_event() {
    auto state = snapshot();
    ordered_json evt;
    evt["type"] = "stats";
    ordered_json stores = ordered_json::array();
    cache::Stats total;
    if (auto* sp = dynamic_cast<host::StoreProvider*>(state->provider.get())) {
        for (auto& store : sp->all_stores()) {
            cache::Stats s =
                co_await net::run_blocking(*ctx_.blocking, [store]() { return store->stats(); });
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
    co_return evt;
}

asio::awaitable<ordered_json> App::rpc_homepage(bool force_revalidate) {
    auto state = snapshot();
    auto* hp = dynamic_cast<host::HomepageProvider*>(state->provider.get());
    if (hp == nullptr)
        throw rpc::RpcError("unavailable", "homepage source is not available in the active mode");

    // Over RPC there is no inbound HTTP request to forward; synthesize a minimal
    // GET. fetch_homepage sets its own Accept header and reads Cache-Control to
    // decide on a forced revalidation, so a shift-reload maps to forceRevalidate.
    Request req;
    req.method = "GET";
    if (force_revalidate) req.headers.set("Cache-Control", "no-cache");

    host::HomepageDocument doc;
    try {
        doc = co_await hp->fetch_homepage(req);
    } catch (const std::exception& e) {
        throw rpc::RpcError("upstream", std::string("fetch homepage source: ") + e.what());
    }
    ordered_json j;
    j["html"] = doc.body;
    j["url"] = doc.url;
    j["statusCode"] = doc.status_code;
    j["cacheStatus"] = doc.cache_status;
    co_return j;
}

void App::close() {
    if (userscript_updater_) userscript_updater_->stop();
    if (game_) game_->close();
    auto state = snapshot();
    if (state && state->provider) state->provider->close();
}

}  // namespace sbc::server
