#pragma once

#include <functional>
#include <map>
#include <string>

#include "config/config.hpp"
#include "server/config_scope.hpp"

namespace sbc::server {

// ConfigChange is the immutable context handed to every config listener: the full
// old and new Config plus the per-scope tier map computed by the scope diff. A
// listener inspects whichever scope(s) it cares about. The referenced configs and
// map outlive every (synchronous) listener call for one apply.
struct ConfigChange {
    const config::Config& old_cfg;
    const config::Config& new_cfg;
    const std::map<std::string, UpdateTier>& changed;  // scope name -> tier
    bool migrate;                                      // user opted into data migration

    bool scope_changed(const std::string& name) const { return changed.count(name) != 0; }
    UpdateTier tier_of(const std::string& name) const {
        auto it = changed.find(name);
        return it == changed.end() ? UpdateTier::Live : it->second;
    }
};

// ConfigPhase orders listener execution around the orchestrator's provider/state
// transaction (App::apply_config). Phases run in numeric order; within a phase
// listeners fire in registration order.
//   PreSwap  - before the provider/state transaction: quiesce (e.g. game DB
//              detach + physical data move). May throw to abort the apply.
//   PostSwap - after the new state_ is live and the config is persisted-pending:
//              re-open / hot-apply (e.g. game DB reopen, game settings COW swap).
//   Notify   - after persistence: fan the change out to subscribers (frontend
//              push, desktop sync). Exception-isolated: one failure never aborts
//              the apply or corrupts state.
enum class ConfigPhase { PreSwap = 0, PostSwap = 1, Notify = 2 };

using ConfigListener = std::function<void(const ConfigChange&)>;

struct ConfigListenerEntry {
    ConfigPhase phase;
    std::string scope_filter;  // "" = any changed scope; else only when that scope changed
    ConfigListener fn;
};

}  // namespace sbc::server
