#pragma once

#include <filesystem>

namespace sbc::platform {

// user_config_dir / user_cache_dir mirror Go's os.UserConfigDir /
// os.UserCacheDir per-platform:
//   macOS:   ~/Library/Application Support  and  ~/Library/Caches
//   Linux:   $XDG_CONFIG_HOME|~/.config     and  $XDG_CACHE_HOME|~/.cache
//   Windows: %AppData%                      and  %LocalAppData%
// Each returns "." as a last resort so the app still runs.
std::filesystem::path user_config_dir();
std::filesystem::path user_cache_dir();

}  // namespace sbc::platform
