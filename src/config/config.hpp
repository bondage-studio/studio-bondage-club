#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cache/router.hpp"
#include "config/field_gen.hpp"

namespace sbc {
class Url;
}

namespace sbc::config {

inline constexpr const char* kModeReverseProxy = "reverse_proxy_cache";
inline constexpr const char* kModePackage = "package_cache";

struct ServerConfig {
    std::string host;
    int port = 0;
    std::string admin_base_path;

    std::string address() const;
};

#define SBC_ServerConfig_FIELDS(X) \
    X(host, host, ALWAYS)          \
    X(port, port, ALWAYS)          \
    X(adminBasePath, admin_base_path, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(ServerConfig)

// StoreConfig defines a named cache store. The name "default" is reserved.
struct StoreConfig {
    std::string name;
    std::string dir;                         // empty -> <CacheConfig.dir>/<name>
    std::int64_t max_size_bytes = 0;         // 0 -> inherit global
    std::optional<int> default_ttl_seconds;  // nullopt -> inherit global
};

#define SBC_StoreConfig_FIELDS(X)              \
    X(name, name, ALWAYS)                      \
    X(dir, dir, OMIT_EMPTY)                    \
    X(maxSizeBytes, max_size_bytes, OMIT_ZERO) \
    X(defaultTTLSeconds, default_ttl_seconds, OMIT_NULL)
SBC_DEFINE_ALLOWED_KEYS(StoreConfig)

struct CacheConfig {
    std::string dir;
    int default_ttl_seconds = 0;
    std::int64_t max_size_bytes = 0;
    // Stale-on-error grace: when an upstream refresh fails, how long past an entry's
    // expiry it may still be served. -1 = unbounded (always), 0 = disabled, >0 =
    // window in seconds.
    int stale_if_error_seconds = -1;
    std::vector<StoreConfig> stores;
    std::vector<cache::CacheRule> rules;
    std::vector<int> cacheable_status_codes;
};

#define SBC_CacheConfig_FIELDS(X)                          \
    X(dir, dir, ALWAYS)                                    \
    X(defaultTTLSeconds, default_ttl_seconds, ALWAYS)      \
    X(maxSizeBytes, max_size_bytes, ALWAYS)                \
    X(staleIfErrorSeconds, stale_if_error_seconds, ALWAYS) \
    X(stores, stores, OMIT_EMPTY)                          \
    X(rules, rules, OMIT_EMPTY)                            \
    X(cacheableStatusCodes, cacheable_status_codes, OMIT_EMPTY)
SBC_DEFINE_ALLOWED_KEYS(CacheConfig)

struct PackageConfig {
    std::string dir;
    std::string manifest_url;
};

#define SBC_PackageConfig_FIELDS(X) \
    X(dir, dir, ALWAYS)             \
    X(manifestUrl, manifest_url, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(PackageConfig)

// GameServerConfig exposes the embedded game server's live policy knobs. Defaults
// match the upstream server values.
struct GameServerConfig {
    int ping_interval_ms = 50000;
    int ping_timeout_ms = 30000;
    int max_payload_bytes = 180000;
    int message_rate_per_sec = 20;
    int ip_connection_limit = 64;
    int ip_connection_rate_per_sec = 2;
    int account_create_per_day = 12;
    int account_create_per_hour = 4;
    int login_pace_ms = 50;
    int login_queue_threshold = 16;
    int pbkdf2_iterations = 100000;
    int password_reset_throttle_ms = 5000;
    std::int64_t relationship_delay_ms = 604800000;  // ownership/lovership/difficulty (7d)
    int server_info_interval_sec = 60;
    int delayed_flush_interval_sec = 300;
    int search_max_results = 240;
    int room_limit_default = 10;
    int room_limit_min = 2;
    int room_limit_max = 20;
    int description_max_len = 300;
    int email_max_len = 100;
    int name_max_len = 20;
    int ownership_notes_max_len = 4000;
};

#define SBC_GameServerConfig_FIELDS(X)                             \
    X(pingIntervalMs, ping_interval_ms, ALWAYS)                    \
    X(pingTimeoutMs, ping_timeout_ms, ALWAYS)                      \
    X(maxPayloadBytes, max_payload_bytes, ALWAYS)                  \
    X(messageRatePerSec, message_rate_per_sec, ALWAYS)             \
    X(ipConnectionLimit, ip_connection_limit, ALWAYS)              \
    X(ipConnectionRatePerSec, ip_connection_rate_per_sec, ALWAYS)  \
    X(accountCreatePerDay, account_create_per_day, ALWAYS)         \
    X(accountCreatePerHour, account_create_per_hour, ALWAYS)       \
    X(loginPaceMs, login_pace_ms, ALWAYS)                          \
    X(loginQueueThreshold, login_queue_threshold, ALWAYS)          \
    X(pbkdf2Iterations, pbkdf2_iterations, ALWAYS)                 \
    X(passwordResetThrottleMs, password_reset_throttle_ms, ALWAYS) \
    X(relationshipDelayMs, relationship_delay_ms, ALWAYS)          \
    X(serverInfoIntervalSec, server_info_interval_sec, ALWAYS)     \
    X(delayedFlushIntervalSec, delayed_flush_interval_sec, ALWAYS) \
    X(searchMaxResults, search_max_results, ALWAYS)                \
    X(roomLimitDefault, room_limit_default, ALWAYS)                \
    X(roomLimitMin, room_limit_min, ALWAYS)                        \
    X(roomLimitMax, room_limit_max, ALWAYS)                        \
    X(descriptionMaxLen, description_max_len, ALWAYS)              \
    X(emailMaxLen, email_max_len, ALWAYS)                          \
    X(nameMaxLen, name_max_len, ALWAYS)                            \
    X(ownershipNotesMaxLen, ownership_notes_max_len, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(GameServerConfig)

#if defined(__ANDROID__)
// AndroidConfig holds knobs that only apply to the Android app host. Compiled in
// only for the NDK build; edited from the panel's Android tab.
struct AndroidConfig {
    // Enable GPU acceleration in the GeckoView (bundled-browser) flavor: forces
    // WebRender + accelerated 2D canvas via Gecko prefs at runtime startup.
    // Applied by gecko/MainActivity at GeckoRuntime creation, so a change only
    // takes effect after an app restart.
    bool hardware_acceleration = true;
};

#define SBC_AndroidConfig_FIELDS(X) X(hardwareAcceleration, hardware_acceleration, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(AndroidConfig)
#endif

#if defined(SBC_DESKTOP)
// DesktopConfig holds knobs that only apply to the CEF desktop host. Compiled in
// only for the desktop build; edited from the panel's Desktop tab.
struct DesktopConfig {
    // GPU compositing in the Chromium window. Read once when building CefSettings
    // at startup, so a change only takes effect after a restart (Restart tier).
    bool hardware_acceleration = true;
    // Initial window size; applied live to the open window (CefWindow::SetSize).
    int window_width = 1280;
    int window_height = 800;
    // Persist the OS window size back into config when the user resizes it.
    bool remember_window_size = true;
};

#define SBC_DesktopConfig_FIELDS(X)                        \
    X(hardwareAcceleration, hardware_acceleration, ALWAYS) \
    X(windowWidth, window_width, ALWAYS)                   \
    X(windowHeight, window_height, ALWAYS)                 \
    X(rememberWindowSize, remember_window_size, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(DesktopConfig)
#endif

// Config is the top-level JSON schema. `mode` is stored as a string so unknown
// values can produce the existing "unsupported mode" error.
struct Config {
    ServerConfig server;
    std::string mode = kModeReverseProxy;
    std::string upstream;
    std::string game_server;
    std::string socks5_proxy;
    bool local_game_server = false;        // first-launch default for the frontend's
                                           // local/remote game-server switch (a browser
                                           // localStorage override takes precedence)
    std::string game_server_storage_path;  // empty -> <cache.dir>/gameserver
    GameServerConfig game_server_settings;
    CacheConfig cache;
    PackageConfig package;
#if defined(__ANDROID__)
    AndroidConfig android;
#endif
#if defined(SBC_DESKTOP)
    DesktopConfig desktop;
#endif

    void validate() const;
};

// Conditionally-compiled members (android) are appended by hand in config.cpp /
// store.cpp under their build guards, so they are intentionally absent here.
#define SBC_Config_FIELDS(X)                                   \
    X(server, server, ALWAYS)                                  \
    X(mode, mode, ALWAYS)                                      \
    X(upstream, upstream, ALWAYS)                              \
    X(gameServer, game_server, ALWAYS)                         \
    X(socks5Proxy, socks5_proxy, ALWAYS)                       \
    X(localGameServer, local_game_server, ALWAYS)              \
    X(gameServerStoragePath, game_server_storage_path, ALWAYS) \
    X(gameServerSettings, game_server_settings, ALWAYS)        \
    X(cache, cache, ALWAYS)                                    \
    X(package, package, ALWAYS)
SBC_DEFINE_ALLOWED_KEYS(Config)

Config default_config();
Config normalize(Config c);

// game_storage_dir resolves the embedded game server's data directory:
// gameServerStoragePath when set, else <cache.dir>/gameserver.
std::string game_storage_dir(const Config& c);

std::string userscript_storage_dir(const Config& c);

// Parsing/validation helpers (also used by the reverse-proxy provider).
// parse_upstream appends a trailing slash to the path.
Url parse_upstream(const std::string& raw);
Url parse_game_server(const std::string& raw);
std::optional<Url> parse_socks5_proxy(const std::string& raw);

}  // namespace sbc::config
