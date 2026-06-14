#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "config/config.hpp"

namespace sbc::server {

// UpdateTier classifies how a config change is applied. Values match the JSON
// field surfaced to the admin panel. With the scope-based mechanism the tier is
// per-scope, not global: each scope reports its own tier and only changed scopes
// do any work.
//   Live     - applied in place, no teardown, in-flight requests untouched.
//   Recreate - the cache provider (and embedded game DB) is rebuilt.
//   Restart  - takes effect only after a process restart (listener address).
enum class UpdateTier { Live = 0, Recreate = 1, Restart = 2 };

// ConfigScope partitions the global Config into an independently-reloadable
// slice. Each scope can extract and merge its slice (for GET / PUT /api/config/
// {scope}) and report the tier a given change requires. The actual reload is
// orchestrated centrally by App::apply_config, which fires work only for the
// scopes whose slice changed, so a change to one scope never disturbs another's
// in-flight requests. `changed` is derived from `get` (slice JSON equality).
struct ConfigScope {
    std::string name;
    std::function<nlohmann::ordered_json(const config::Config&)> get;
    std::function<void(config::Config&, const nlohmann::ordered_json&)> set;
    std::function<UpdateTier(const config::Config& old_cfg, const config::Config& new_cfg)> tier;

    bool changed(const config::Config& a, const config::Config& b) const { return get(a) != get(b); }
};

}  // namespace sbc::server
