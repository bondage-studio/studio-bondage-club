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

// Default cache routing: content-addressed add-ons use separate stores; the game
// body is keyed by R-number and revalidated across releases.
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
        if (v <= 0)
            throw ValidationError(std::string("gameServerSettings ") + name + " must be positive");
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

#if defined(SBC_DESKTOP)
    if (desktop.window_width <= 0 || desktop.window_height <= 0) {
        throw ValidationError("desktop windowWidth and windowHeight must be positive");
    }
#endif
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

// JSON (de)serialization uses camelCase keys and omits empty values. The field
// lists live in config.hpp (SBC_<Type>_FIELDS); the bodies are generated so the
// wire format and the strict allowed-key set can never drift from the structs.

SBC_DEFINE_STRUCT_JSON(ServerConfig)
SBC_DEFINE_STRUCT_JSON(StoreConfig)
SBC_DEFINE_STRUCT_JSON(CacheConfig)
SBC_DEFINE_STRUCT_JSON(PackageConfig)
// Every game-settings field is ALWAYS-emit so the admin panel always has
// concrete values to render and edit.
SBC_DEFINE_STRUCT_JSON(GameServerConfig)

#if defined(__ANDROID__)
SBC_DEFINE_STRUCT_JSON(AndroidConfig)
#endif

#if defined(SBC_DESKTOP)
SBC_DEFINE_STRUCT_JSON(DesktopConfig)
#endif

// Config mixes generated fields with conditionally-compiled members (android,
// desktop), so its body is hand-rolled around the shared field-list fragments.
void to_json(ordered_json& j, const Config& v) {
    SBC_TO_JSON_BODY(Config)
#if defined(__ANDROID__)
    j["android"] = v.android;
#endif
#if defined(SBC_DESKTOP)
    j["desktop"] = v.desktop;
#endif
}

void from_json(const ordered_json& j, Config& v) {
    // Merge into the existing (default-initialized) structs so absent keys keep
    // their defaults.
    SBC_FROM_JSON_BODY(Config)
#if defined(__ANDROID__)
    read_field(j, "android", v.android);
#endif
#if defined(SBC_DESKTOP)
    read_field(j, "desktop", v.desktop);
#endif
}

}  // namespace sbc::config
