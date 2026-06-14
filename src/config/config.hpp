#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cache/router.hpp"

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

    std::string address() const;  // host:port
};

// StoreConfig defines a named cache store. The name "default" is reserved.
struct StoreConfig {
    std::string name;
    std::string dir;                          // empty -> <CacheConfig.dir>/<name>
    std::int64_t max_size_bytes = 0;          // 0 -> inherit global
    std::optional<int> default_ttl_seconds;   // nullopt -> inherit global
};

struct CacheConfig {
    std::string dir;
    int default_ttl_seconds = 0;
    std::int64_t max_size_bytes = 0;
    std::vector<StoreConfig> stores;          // additional named stores
    std::vector<cache::CacheRule> rules;      // policy routing; first match wins
};

struct PackageConfig {
    std::string dir;
    std::string manifest_url;
};

// GameServerConfig exposes the embedded game server's policy knobs (formerly
// hardcoded constants in src/server/gameserver/**). Defaults reproduce the
// original BC server's values, so behaviour is unchanged until edited. Changes
// apply live with no teardown (read per new connection / per tick / per new
// password hash).
struct GameServerConfig {
    // Engine.IO transport.
    int ping_interval_ms = 50000;
    int ping_timeout_ms = 30000;
    int max_payload_bytes = 180000;
    // Rate limits.
    int message_rate_per_sec = 20;
    int ip_connection_limit = 64;
    int ip_connection_rate_per_sec = 2;
    // Accounts.
    int account_create_per_day = 12;
    int account_create_per_hour = 4;
    int login_pace_ms = 50;
    int login_queue_threshold = 16;
    int pbkdf2_iterations = 100000;
    int password_reset_throttle_ms = 5000;
    std::int64_t relationship_delay_ms = 604800000;  // ownership/lovership/difficulty (7d)
    // Periodic timers.
    int server_info_interval_sec = 60;
    int delayed_flush_interval_sec = 300;
    // Rooms.
    int search_max_results = 240;
    int room_limit_default = 10;
    int room_limit_min = 2;
    int room_limit_max = 20;
    int description_max_len = 300;
    // Text limits.
    int email_max_len = 100;
    int name_max_len = 20;
    int ownership_notes_max_len = 4000;
};

// Config mirrors the Go top-level Config. `mode` is stored as a string to
// preserve the exact "unsupported mode" error for unknown values.
struct Config {
    ServerConfig server;
    std::string mode = kModeReverseProxy;
    std::string upstream;
    std::string game_server;
    std::string socks5_proxy;
    bool local_game_server = false;  // first-launch default for the frontend's
                                     // local/remote game-server switch (a browser
                                     // localStorage override takes precedence)
    GameServerConfig game_server_settings;  // embedded game server policy knobs
    CacheConfig cache;
    PackageConfig package;

    // validate throws sbc::ValidationError with a message identical to the Go
    // implementation (surfaced to the admin panel).
    void validate() const;
};

Config default_config();
Config normalize(Config c);

// Parsing/validation helpers (also used by the reverse-proxy provider).
// parse_upstream appends a trailing slash to the path, like Go.
Url parse_upstream(const std::string& raw);
Url parse_game_server(const std::string& raw);
// parse_socks5_proxy returns nullopt for an empty proxy string.
std::optional<Url> parse_socks5_proxy(const std::string& raw);

}  // namespace sbc::config
