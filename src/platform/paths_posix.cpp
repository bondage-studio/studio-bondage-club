#include "platform/paths.hpp"

#include <cstdlib>

namespace sbc::platform {

namespace {

std::filesystem::path home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return std::filesystem::path(h);
    return std::filesystem::path(".");
}

std::filesystem::path env_or(const char* var, const std::filesystem::path& fallback) {
    if (const char* v = std::getenv(var); v && *v) return std::filesystem::path(v);
    return fallback;
}

}  // namespace

std::filesystem::path user_config_dir() {
#if defined(__APPLE__)
    return home_dir() / "Library" / "Application Support";
#else
    return env_or("XDG_CONFIG_HOME", home_dir() / ".config");
#endif
}

std::filesystem::path user_cache_dir() {
#if defined(__APPLE__)
    return home_dir() / "Library" / "Caches";
#else
    return env_or("XDG_CACHE_HOME", home_dir() / ".cache");
#endif
}

}  // namespace sbc::platform
