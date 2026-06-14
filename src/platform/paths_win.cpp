#include "platform/paths.hpp"

#include <cstdlib>

namespace sbc::platform {

namespace {

std::filesystem::path env_or(const char* var, std::filesystem::path fallback) {
    if (const char* v = std::getenv(var); v && *v) return std::filesystem::path(v);
    return fallback;
}

}  // namespace

// Windows: %AppData% (roaming) for config, %LocalAppData% for cache, matching
// Go's os.UserConfigDir / os.UserCacheDir.
std::filesystem::path user_config_dir() { return env_or("APPDATA", "."); }
std::filesystem::path user_cache_dir() { return env_or("LOCALAPPDATA", "."); }

}  // namespace sbc::platform
