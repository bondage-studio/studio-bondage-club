#include <nlohmann/json.hpp>

#include "common/json_merge.hpp"
#include "test_framework.hpp"

using namespace sbc;
using json = nlohmann::json;

// A plain (dot-free) key is assigned literally.
SBC_TEST(json_merge_plain_key) {
    json root = json::object();
    merge_set_field(root, "Title", "owner");
    CHECK(root["Title"] == "owner");
}

// The headline case: a dotted key from AccountUpdate must land at a nested path
// (mirroring the old MongoDB `$set` injection), NOT as a literal "a.b" key.
SBC_TEST(json_merge_dotted_key_nests) {
    json root = json::object();
    merge_set_field(root, "ExtensionSettings.BCX", json{{"Version", 42}});
    CHECK(!root.contains("ExtensionSettings.BCX"));
    CHECK(root["ExtensionSettings"]["BCX"]["Version"] == 42);
}

// A nested set must preserve sibling sub-keys (the partial-update behaviour BCX
// relies on, rather than replacing the whole ExtensionSettings object).
SBC_TEST(json_merge_dotted_key_preserves_siblings) {
    json root = {{"ExtensionSettings", {{"Other", "keep"}}}};
    merge_set_field(root, "ExtensionSettings.BCX", "new");
    CHECK(root["ExtensionSettings"]["Other"] == "keep");
    CHECK(root["ExtensionSettings"]["BCX"] == "new");
}

// Deep paths create every missing intermediate object.
SBC_TEST(json_merge_deep_path) {
    json root = json::object();
    merge_set_field(root, "a.b.c.d", 1);
    CHECK(root["a"]["b"]["c"]["d"] == 1);
}

// An intermediate value that is not an object is replaced so the path can win.
SBC_TEST(json_merge_intermediate_not_object) {
    json root = {{"a", 5}};
    merge_set_field(root, "a.b", "x");
    CHECK(root["a"]["b"] == "x");
}

// merge_set_fields applies every member with the same semantics.
SBC_TEST(json_merge_fields_object) {
    json root = json::object();
    json fields = {{"Plain", 1}, {"ExtensionSettings.BCX", 2}};
    merge_set_fields(root, fields);
    CHECK(root["Plain"] == 1);
    CHECK(root["ExtensionSettings"]["BCX"] == 2);
}