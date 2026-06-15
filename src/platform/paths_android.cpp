#include "platform/paths.hpp"

#include <cstdlib>

namespace sbc::platform {

namespace {

std::filesystem::path env_or(const char* var, std::filesystem::path fallback) {
    if (const char* v = std::getenv(var); v && *v) return std::filesystem::path(v);
    return fallback;
}

}  // namespace

// Android: app-private directories are supplied by the host activity at runtime
// via these env vars (set by the NDK integration). Fall back to cwd-relative
// paths so headless/test runs still work.
std::filesystem::path user_config_dir() {
    return env_or("SBC_CONFIG_DIR", "files");
}
std::filesystem::path user_cache_dir() {
    return env_or("SBC_CACHE_DIR", "cache");
}

}  // namespace sbc::platform
