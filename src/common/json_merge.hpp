#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace sbc {

// merge_set_field applies a single field update with MongoDB `$set` semantics.
//
// The original (Node.js) BondageClub server persisted account updates via
// `AccountCollection.updateOne({...}, { $set: data })`. MongoDB interprets a dot
// in a `$set` key as a nested path separator, so a client sending the field
// "ExtensionSettings.BCX" did NOT create a top-level key by that literal name —
// it set `ExtensionSettings.BCX` as a *partial* update, i.e. only the `BCX`
// sub-field, leaving the other ExtensionSettings entries intact.
//
// Extensions such as BCX deliberately rely on this behaviour to write a single
// sub-key without round-tripping the whole ExtensionSettings object. To stay
// bug-for-bug compatible we reproduce it here:
//   - A key with no '.' is assigned literally (the common case).
//   - A key with dots walks/creates intermediate objects and assigns only the
//     leaf, preserving sibling keys at every level. An intermediate value that
//     is missing or not an object is (re)created as an empty object, matching
//     the "path wins" outcome the old server effectively produced.
void merge_set_field(nlohmann::json& root, const std::string& key,
                     const nlohmann::json& value);

// merge_set_fields applies merge_set_field for every member of `fields` (which
// must be a JSON object) into `root`.
void merge_set_fields(nlohmann::json& root, const nlohmann::json& fields);

}  // namespace sbc