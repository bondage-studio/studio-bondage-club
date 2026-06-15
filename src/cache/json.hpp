#pragma once

#include <nlohmann/json.hpp>

#include "cache/router.hpp"

namespace sbc::cache {

// to_json/from_json for CacheRule using ordered_json so config output preserves
// field order. Output omits empty/default fields.
void to_json(nlohmann::ordered_json& j, const CacheRule& r);
void from_json(const nlohmann::ordered_json& j, CacheRule& r);

}  // namespace sbc::cache
