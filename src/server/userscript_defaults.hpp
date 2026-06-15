#pragma once

#include <vector>

#include <nlohmann/json.hpp>

namespace sbc::server {

// Built-in (default) userscripts seeded into the store on first run. From the
// API their id, name, and source URL (downloadURL/updateURL) are immutable and
// they cannot be deleted; users may still edit the source, toggle enable /
// auto-update, and apply updates. See App's constructor (seeding via
// UserscriptStore::ensure_builtin) and handle_userscripts (protection).
std::vector<nlohmann::json> builtin_userscripts();

}  // namespace sbc::server
