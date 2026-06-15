#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace sbc {

// merge_set_field applies one MongoDB-style `$set` update. Plain keys assign a
// literal field; dotted keys update/create nested objects while preserving
// siblings, which extensions rely on for partial paths such as
// "ExtensionSettings.BCX".
void merge_set_field(nlohmann::json& root, const std::string& key,
                     const nlohmann::json& value);

// merge_set_fields applies merge_set_field for every member of `fields` (which
// must be a JSON object) into `root`.
void merge_set_fields(nlohmann::json& root, const nlohmann::json& fields);

}  // namespace sbc
