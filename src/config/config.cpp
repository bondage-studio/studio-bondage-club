#include "config/config.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <set>

#include "cache/json.hpp"
#include "config/json.hpp"
#include "common/error.hpp"
#include "common/url.hpp"
#include "platform/paths.hpp"

namespace sbc::config {

using nlohmann::ordered_json;

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

std::string ServerConfig::address() const {
    std::string p = std::to_string(port);
    if (host.find(':') != std::string::npos) return "[" + host + "]:" + p;
    return host + ":" + p;
}

namespace {

// default_cache_stores / default_cache_rules seed a fresh install with caching
// that actually works out of the box. Desktop users could previously copy a
// tuned config.json, but a packaged host (notably Android) only ever gets the
// generated default config, so an empty rule set meant nothing got cached. These
// mirror run/config.json: separate "echo" / "mpa" stores for the content-
// addressed add-ons and an "assets" store for the game body, keyed by the
// game's R-number and revalidated across releases.
std::vector<StoreConfig> default_cache_stores() {
    std::vector<StoreConfig> stores;
    for (const char* name : {"assets", "echo", "mpa"}) {
        StoreConfig s;
        s.name = name;
        stores.push_back(std::move(s));
    }
    return stores;
}

std::vector<cache::CacheRule> default_cache_rules() {
    constexpr const char* kImmutable = "public, max-age=31536000, immutable";
    std::vector<cache::CacheRule> rules;

    // Echo clothing/activity extensions served from GitHub Pages (?v= token).
    {
        cache::CacheRule r;
        r.host = "sugarchain-studio.github.io";
        r.store = "echo";
        r.cache_control = kImmutable;
        r.force_cache = true;
        r.version = "query:v";
        r.key_pattern = "re:^/(echo-(?:clothing|activity)-ext)/(.*)$";
        r.key_template = "$1/$2";
        rules.push_back(std::move(r));
    }
    // Same extensions served from jsDelivr (@version in the path); canonicalised
    // to the same key so both URL shapes share one cached copy.
    {
        cache::CacheRule r;
        r.host = "cdn.jsdelivr.net";
        r.store = "echo";
        r.cache_control = kImmutable;
        r.force_cache = true;
        r.version = "re:@([^/]+)/";
        r.key_pattern = "re:^/gh/SugarChain-Studio/(echo-(?:clothing|activity)-ext)@[^/]+/(.*)$";
        r.key_template = "$1/$2";
        rules.push_back(std::move(r));
    }
    // MPA audio assets.
    {
        cache::CacheRule r;
        r.host = "mayathefoxy.github.io";
        r.path_prefix = "/MPA/assets/";
        r.path_pattern = "re:\\.mp3$";
        r.store = "mpa";
        r.key_mode = "path";
        r.cache_control = kImmutable;
        r.force_cache = true;
        rules.push_back(std::move(r));
    }
    // Catch-all for the game body itself, keyed by path and revalidated whenever
    // the R-number bumps so unchanged files 304 and carry over across releases.
    {
        cache::CacheRule r;
        r.store = "assets";
        r.key_mode = "path";
        r.cache_control = "public, max-age=86400";
        r.force_cache = true;
        r.version = "re:/(R\\d+)/BondageClub/";
        r.version_revalidate = true;
        rules.push_back(std::move(r));
    }
    return rules;
}

}  // namespace

Config default_config() {
    auto cache_root = platform::user_cache_dir();
    auto config_root = platform::user_config_dir();
    auto base_cache = cache_root / "studio-bondage-club";
    auto base_config = config_root / "studio-bondage-club";

    Config c;
    c.server.host = "127.0.0.1";
    c.server.port = 8080;
    c.server.admin_base_path = "/studio/";
    c.mode = kModeReverseProxy;
    c.upstream = "https://www.bondageprojects.elementfx.com/R129/BondageClub/";
    c.game_server = "https://bondage-club-server.herokuapp.com/";
    c.cache.dir = (base_cache / "http-cache").string();
    c.cache.default_ttl_seconds = 0;
    c.cache.max_size_bytes = 5LL * 1024 * 1024 * 1024;
    c.cache.stores = default_cache_stores();
    c.cache.rules = default_cache_rules();
    c.package.dir = (base_config / "packages").string();
    return c;
}

std::string game_storage_dir(const Config& c) {
    if (!c.game_server_storage_path.empty()) return c.game_server_storage_path;
    std::string base = c.cache.dir.empty() ? "." : c.cache.dir;
    return (std::filesystem::path(base) / "gameserver").generic_string();
}

std::string userscript_storage_dir(const Config& c) {
    std::string base = c.cache.dir.empty() ? "." : c.cache.dir;
    return (std::filesystem::path(base) / "userscripts").generic_string();
}

Url parse_upstream(const std::string& raw) {
    if (raw.empty()) throw ValidationError("upstream is required");
    auto u = Url::try_parse(raw);
    if (!u) throw ValidationError("invalid upstream: " + raw);
    std::string scheme = u->scheme();
    if (scheme != "http" && scheme != "https") {
        throw ValidationError("upstream must use http or https");
    }
    if (u->host().empty()) throw ValidationError("upstream host is required");
    u->ensure_trailing_slash();
    return *u;
}

Url parse_game_server(const std::string& raw) {
    if (raw.empty()) throw ValidationError("gameServer is required");
    auto u = Url::try_parse(raw);
    if (!u) throw ValidationError("invalid gameServer: " + raw);
    std::string scheme = u->scheme();
    if (scheme != "http" && scheme != "https") {
        throw ValidationError("gameServer must use http or https");
    }
    if (u->host().empty()) throw ValidationError("gameServer host is required");
    return *u;
}

std::optional<Url> parse_socks5_proxy(const std::string& raw_in) {
    std::string raw = trim(raw_in);
    if (raw.empty()) return std::nullopt;
    if (raw.find("://") == std::string::npos) raw = "socks5://" + raw;
    auto u = Url::try_parse(raw);
    if (!u) throw ValidationError("invalid socks5Proxy: " + raw);
    std::string scheme = u->scheme();
    if (scheme != "socks5" && scheme != "socks5h") {
        throw ValidationError("socks5Proxy must use socks5 or socks5h");
    }
    if (u->host().empty()) throw ValidationError("socks5Proxy host is required");
    if (u->port_string().empty()) throw ValidationError("socks5Proxy host must include a port");
    std::string path = u->path();
    if (!path.empty() && path != "/") throw ValidationError("socks5Proxy must not include a path");
    if (!u->query().empty() || !u->fragment().empty()) {
        throw ValidationError("socks5Proxy must not include query or fragment");
    }
    return u;
}

void Config::validate() const {
    if (server.host.empty()) throw ValidationError("server host is required");
    if (server.port <= 0 || server.port > 65535) {
        throw ValidationError("server port must be between 1 and 65535, got " +
                              std::to_string(server.port));
    }
    if (server.admin_base_path.empty() || server.admin_base_path.front() != '/' ||
        server.admin_base_path.back() != '/') {
        throw ValidationError("server adminBasePath must start and end with /");
    }
    parse_socks5_proxy(socks5_proxy);
    parse_game_server(game_server);

    if (mode == kModeReverseProxy) {
        parse_upstream(upstream);
    } else if (mode == kModePackage) {
        if (package.dir.empty()) throw ValidationError("package dir is required");
    } else {
        throw ValidationError("unsupported mode \"" + mode + "\"");
    }

    if (cache.dir.empty()) throw ValidationError("cache dir is required");
    if (cache.default_ttl_seconds < 0) {
        throw ValidationError("cache defaultTTLSeconds cannot be negative");
    }
    if (cache.max_size_bytes < 0) throw ValidationError("cache maxSizeBytes cannot be negative");

    std::set<std::string> names{"default"};
    for (std::size_t i = 0; i < cache.stores.size(); ++i) {
        const auto& sc = cache.stores[i];
        if (sc.name.empty()) {
            throw ValidationError("cache.stores[" + std::to_string(i) + "]: name is required");
        }
        if (sc.name == "default") {
            throw ValidationError("cache.stores[" + std::to_string(i) +
                                  "]: name \"default\" is reserved");
        }
        if (names.count(sc.name)) {
            throw ValidationError("cache.stores[" + std::to_string(i) +
                                  "]: duplicate store name \"" + sc.name + "\"");
        }
        names.insert(sc.name);
    }
    for (const auto& rule : cache.rules) {
        if (!rule.store.empty() && !names.count(rule.store)) {
            throw ValidationError("cache rule references unknown store \"" + rule.store + "\"");
        }
    }
    try {
        cache::PolicyRouter router(cache.rules);
    } catch (const Error& e) {
        throw ValidationError(std::string("cache rules: ") + e.what());
    }

    const auto& g = game_server_settings;
    auto require_pos = [](const char* name, std::int64_t v) {
        if (v <= 0) throw ValidationError(std::string("gameServerSettings ") + name +
                                          " must be positive");
    };
    require_pos("pingIntervalMs", g.ping_interval_ms);
    require_pos("pingTimeoutMs", g.ping_timeout_ms);
    require_pos("maxPayloadBytes", g.max_payload_bytes);
    require_pos("messageRatePerSec", g.message_rate_per_sec);
    require_pos("ipConnectionLimit", g.ip_connection_limit);
    require_pos("ipConnectionRatePerSec", g.ip_connection_rate_per_sec);
    require_pos("accountCreatePerDay", g.account_create_per_day);
    require_pos("accountCreatePerHour", g.account_create_per_hour);
    require_pos("loginPaceMs", g.login_pace_ms);
    require_pos("loginQueueThreshold", g.login_queue_threshold);
    require_pos("pbkdf2Iterations", g.pbkdf2_iterations);
    require_pos("passwordResetThrottleMs", g.password_reset_throttle_ms);
    require_pos("relationshipDelayMs", g.relationship_delay_ms);
    require_pos("serverInfoIntervalSec", g.server_info_interval_sec);
    require_pos("delayedFlushIntervalSec", g.delayed_flush_interval_sec);
    require_pos("searchMaxResults", g.search_max_results);
    require_pos("descriptionMaxLen", g.description_max_len);
    require_pos("emailMaxLen", g.email_max_len);
    require_pos("nameMaxLen", g.name_max_len);
    require_pos("ownershipNotesMaxLen", g.ownership_notes_max_len);
    if (g.room_limit_min < 1) throw ValidationError("gameServerSettings roomLimitMin must be >= 1");
    if (g.room_limit_min > g.room_limit_default || g.room_limit_default > g.room_limit_max) {
        throw ValidationError(
            "gameServerSettings requires roomLimitMin <= roomLimitDefault <= roomLimitMax");
    }
}

Config normalize(Config c) {
    c.upstream = trim(c.upstream);
    c.game_server = trim(c.game_server);
    c.socks5_proxy = trim(c.socks5_proxy);
    c.game_server_storage_path = trim(c.game_server_storage_path);
    if (c.game_server.empty()) c.game_server = default_config().game_server;
    if (c.server.host.empty()) c.server.host = "127.0.0.1";
    if (c.server.port == 0) c.server.port = 8080;
    if (c.server.admin_base_path.empty()) c.server.admin_base_path = "/studio/";
    if (c.mode.empty()) c.mode = kModeReverseProxy;
    if (c.cache.max_size_bytes == 0) c.cache.max_size_bytes = default_config().cache.max_size_bytes;
    if (c.cache.dir.empty()) c.cache.dir = default_config().cache.dir;
    if (c.package.dir.empty()) c.package.dir = default_config().package.dir;
    return c;
}

// JSON (de)serialization uses camelCase keys and omits empty values to match Go.

void to_json(ordered_json& j, const ServerConfig& s) {
    j = ordered_json::object();
    j["host"] = s.host;
    j["port"] = s.port;
    j["adminBasePath"] = s.admin_base_path;
}

void from_json(const ordered_json& j, ServerConfig& s) {
    if (auto it = j.find("host"); it != j.end()) s.host = it->get<std::string>();
    if (auto it = j.find("port"); it != j.end()) s.port = it->get<int>();
    if (auto it = j.find("adminBasePath"); it != j.end()) s.admin_base_path = it->get<std::string>();
}

void to_json(ordered_json& j, const StoreConfig& s) {
    j = ordered_json::object();
    j["name"] = s.name;
    if (!s.dir.empty()) j["dir"] = s.dir;
    if (s.max_size_bytes != 0) j["maxSizeBytes"] = s.max_size_bytes;
    if (s.default_ttl_seconds.has_value()) j["defaultTTLSeconds"] = *s.default_ttl_seconds;
}

void from_json(const ordered_json& j, StoreConfig& s) {
    if (auto it = j.find("name"); it != j.end()) s.name = it->get<std::string>();
    if (auto it = j.find("dir"); it != j.end()) s.dir = it->get<std::string>();
    if (auto it = j.find("maxSizeBytes"); it != j.end()) s.max_size_bytes = it->get<std::int64_t>();
    if (auto it = j.find("defaultTTLSeconds"); it != j.end() && !it->is_null())
        s.default_ttl_seconds = it->get<int>();
}

void to_json(ordered_json& j, const CacheConfig& c) {
    j = ordered_json::object();
    j["dir"] = c.dir;
    j["defaultTTLSeconds"] = c.default_ttl_seconds;
    j["maxSizeBytes"] = c.max_size_bytes;
    if (!c.stores.empty()) j["stores"] = c.stores;
    if (!c.rules.empty()) j["rules"] = c.rules;
}

void from_json(const ordered_json& j, CacheConfig& c) {
    if (auto it = j.find("dir"); it != j.end()) c.dir = it->get<std::string>();
    if (auto it = j.find("defaultTTLSeconds"); it != j.end()) c.default_ttl_seconds = it->get<int>();
    if (auto it = j.find("maxSizeBytes"); it != j.end()) c.max_size_bytes = it->get<std::int64_t>();
    if (auto it = j.find("stores"); it != j.end()) c.stores = it->get<std::vector<StoreConfig>>();
    if (auto it = j.find("rules"); it != j.end())
        c.rules = it->get<std::vector<cache::CacheRule>>();
}

void to_json(ordered_json& j, const PackageConfig& p) {
    j = ordered_json::object();
    j["dir"] = p.dir;
    j["manifestUrl"] = p.manifest_url;
}

void from_json(const ordered_json& j, PackageConfig& p) {
    if (auto it = j.find("dir"); it != j.end()) p.dir = it->get<std::string>();
    if (auto it = j.find("manifestUrl"); it != j.end()) p.manifest_url = it->get<std::string>();
}

void to_json(ordered_json& j, const GameServerConfig& g) {
    // Emit every field (unlike the omitempty structs) so the admin panel always
    // has concrete values to render and edit.
    j = ordered_json::object();
    j["pingIntervalMs"] = g.ping_interval_ms;
    j["pingTimeoutMs"] = g.ping_timeout_ms;
    j["maxPayloadBytes"] = g.max_payload_bytes;
    j["messageRatePerSec"] = g.message_rate_per_sec;
    j["ipConnectionLimit"] = g.ip_connection_limit;
    j["ipConnectionRatePerSec"] = g.ip_connection_rate_per_sec;
    j["accountCreatePerDay"] = g.account_create_per_day;
    j["accountCreatePerHour"] = g.account_create_per_hour;
    j["loginPaceMs"] = g.login_pace_ms;
    j["loginQueueThreshold"] = g.login_queue_threshold;
    j["pbkdf2Iterations"] = g.pbkdf2_iterations;
    j["passwordResetThrottleMs"] = g.password_reset_throttle_ms;
    j["relationshipDelayMs"] = g.relationship_delay_ms;
    j["serverInfoIntervalSec"] = g.server_info_interval_sec;
    j["delayedFlushIntervalSec"] = g.delayed_flush_interval_sec;
    j["searchMaxResults"] = g.search_max_results;
    j["roomLimitDefault"] = g.room_limit_default;
    j["roomLimitMin"] = g.room_limit_min;
    j["roomLimitMax"] = g.room_limit_max;
    j["descriptionMaxLen"] = g.description_max_len;
    j["emailMaxLen"] = g.email_max_len;
    j["nameMaxLen"] = g.name_max_len;
    j["ownershipNotesMaxLen"] = g.ownership_notes_max_len;
}

void from_json(const ordered_json& j, GameServerConfig& g) {
    auto get_i = [&](const char* k, int& dst) {
        if (auto it = j.find(k); it != j.end()) dst = it->get<int>();
    };
    get_i("pingIntervalMs", g.ping_interval_ms);
    get_i("pingTimeoutMs", g.ping_timeout_ms);
    get_i("maxPayloadBytes", g.max_payload_bytes);
    get_i("messageRatePerSec", g.message_rate_per_sec);
    get_i("ipConnectionLimit", g.ip_connection_limit);
    get_i("ipConnectionRatePerSec", g.ip_connection_rate_per_sec);
    get_i("accountCreatePerDay", g.account_create_per_day);
    get_i("accountCreatePerHour", g.account_create_per_hour);
    get_i("loginPaceMs", g.login_pace_ms);
    get_i("loginQueueThreshold", g.login_queue_threshold);
    get_i("pbkdf2Iterations", g.pbkdf2_iterations);
    get_i("passwordResetThrottleMs", g.password_reset_throttle_ms);
    if (auto it = j.find("relationshipDelayMs"); it != j.end())
        g.relationship_delay_ms = it->get<std::int64_t>();
    get_i("serverInfoIntervalSec", g.server_info_interval_sec);
    get_i("delayedFlushIntervalSec", g.delayed_flush_interval_sec);
    get_i("searchMaxResults", g.search_max_results);
    get_i("roomLimitDefault", g.room_limit_default);
    get_i("roomLimitMin", g.room_limit_min);
    get_i("roomLimitMax", g.room_limit_max);
    get_i("descriptionMaxLen", g.description_max_len);
    get_i("emailMaxLen", g.email_max_len);
    get_i("nameMaxLen", g.name_max_len);
    get_i("ownershipNotesMaxLen", g.ownership_notes_max_len);
}

void to_json(ordered_json& j, const Config& c) {
    j = ordered_json::object();
    j["server"] = c.server;
    j["mode"] = c.mode;
    j["upstream"] = c.upstream;
    j["gameServer"] = c.game_server;
    j["socks5Proxy"] = c.socks5_proxy;
    j["localGameServer"] = c.local_game_server;
    j["gameServerStoragePath"] = c.game_server_storage_path;
    j["gameServerSettings"] = c.game_server_settings;
    j["cache"] = c.cache;
    j["package"] = c.package;
}

void from_json(const ordered_json& j, Config& c) {
    // Merge into the existing (default-initialized) structs so absent keys keep
    // their defaults, matching Go's json.Unmarshal-into-existing behaviour.
    if (auto it = j.find("server"); it != j.end()) from_json(*it, c.server);
    if (auto it = j.find("mode"); it != j.end()) c.mode = it->get<std::string>();
    if (auto it = j.find("upstream"); it != j.end()) c.upstream = it->get<std::string>();
    if (auto it = j.find("gameServer"); it != j.end()) c.game_server = it->get<std::string>();
    if (auto it = j.find("socks5Proxy"); it != j.end()) c.socks5_proxy = it->get<std::string>();
    if (auto it = j.find("localGameServer"); it != j.end()) c.local_game_server = it->get<bool>();
    if (auto it = j.find("gameServerStoragePath"); it != j.end())
        c.game_server_storage_path = it->get<std::string>();
    if (auto it = j.find("gameServerSettings"); it != j.end()) from_json(*it, c.game_server_settings);
    if (auto it = j.find("cache"); it != j.end()) from_json(*it, c.cache);
    if (auto it = j.find("package"); it != j.end()) from_json(*it, c.package);
}

}  // namespace sbc::config
