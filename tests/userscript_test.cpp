#include <atomic>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "server/userscript_metadata.hpp"
#include "server/userscript_store.hpp"
#include "test_framework.hpp"

using namespace sbc;
using namespace sbc::server;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir(const std::string& tag) {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() /
                ("sbc-us-test-" + tag + "-" + std::to_string(counter.fetch_add(1)));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

json sample_script(const std::string& id, int sort_order = 0) {
    return json{
        {"id", id},
        {"name", "Script " + id},
        {"source", "// ==UserScript==\n// @version 1.0.0\n// ==/UserScript==\nconsole.log(1)"},
        {"enabled", true},
        {"autoUpdate", false},
        {"version", "1.0.0"},
        {"sortOrder", sort_order}};
}

}  // namespace

// ---- metadata parsing + version comparison ---------------------------------

SBC_TEST(userscript_metadata_parse_version) {
    std::string src =
        "// ==UserScript==\n"
        "// @name        My Script\n"
        "// @version     2.3.1\n"
        "// @downloadURL https://example.com/s.user.js\n"
        "// @updateURL   https://example.com/s.meta.js\n"
        "// ==/UserScript==\n"
        "console.log('body');\n";
    CHECK(parse_metadata_field(src, "version") == "2.3.1");
    CHECK(parse_metadata_field(src, "name") == "My Script");
    CHECK(parse_metadata_field(src, "downloadURL") == "https://example.com/s.user.js");
    CHECK(parse_metadata_field(src, "updateURL") == "https://example.com/s.meta.js");
    CHECK(parse_metadata_field(src, "missing").empty());
}

SBC_TEST(userscript_metadata_no_block) {
    CHECK(parse_metadata_field("console.log('no metadata here')", "version").empty());
}

SBC_TEST(userscript_version_compare) {
    CHECK(version_newer("1.0.1", "1.0.0"));
    CHECK(version_newer("1.1.0", "1.0.9"));
    CHECK(version_newer("2.0.0", "1.9.9"));
    CHECK(version_newer("1.0.0", ""));        // any version beats none
    CHECK(version_newer("1.0.10", "1.0.9"));  // numeric, not lexical
    CHECK(!version_newer("1.0.0", "1.0.0"));  // equal
    CHECK(!version_newer("1.0.0", "1.0.1"));  // older
    CHECK(!version_newer("", "1.0.0"));       // empty candidate
    CHECK(!version_newer("", ""));
}

// ---- store round-trips -----------------------------------------------------

SBC_TEST(userscript_store_put_get_list) {
    auto dir = make_temp_dir("scripts");
    auto store = UserscriptStore::open(dir.string());

    CHECK(store->list().empty());
    CHECK(!store->get("a").has_value());

    store->put(sample_script("b", 2));
    store->put(sample_script("a", 1));
    store->put(sample_script("c", 3));

    auto got = store->get("a");
    CHECK(got.has_value());
    CHECK(got->value("name", "") == "Script a");

    auto all = store->list();
    CHECK(all.size() == 3);
    // sorted by sortOrder
    CHECK(all[0].value("id", "") == "a");
    CHECK(all[1].value("id", "") == "b");
    CHECK(all[2].value("id", "") == "c");
}

SBC_TEST(userscript_store_reorder) {
    auto dir = make_temp_dir("reorder");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a", 0));
    store->put(sample_script("b", 1));
    store->put(sample_script("c", 2));

    store->reorder({"c", "a", "b"});
    auto all = store->list();
    CHECK(all[0].value("id", "") == "c");
    CHECK(all[1].value("id", "") == "a");
    CHECK(all[2].value("id", "") == "b");
}

SBC_TEST(userscript_store_values) {
    auto dir = make_temp_dir("values");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a"));

    CHECK(store->values("a").empty());
    store->set_value("a", "count", "42");
    store->set_value("a", "label", "\"hello\"");
    store->set_value("a", "obj", R"({"x":1,"y":[2,3]})");

    json vals = store->values("a");
    CHECK(vals.size() == 3);
    CHECK(vals["count"] == 42);
    CHECK(vals["label"] == "hello");
    CHECK(vals["obj"]["y"][1] == 3);

    store->del_value("a", "count");
    json after = store->values("a");
    CHECK(after.size() == 2);
    CHECK(!after.contains("count"));
}

SBC_TEST(userscript_store_remove_clears_ranges) {
    auto dir = make_temp_dir("remove");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a"));
    store->put(sample_script("b"));
    store->set_value("a", "k1", "1");
    store->set_value("a", "k2", "2");
    store->set_pending("a", json{{"version", "2.0.0"}, {"source", "x"}, {"fetchedAt", 1}});

    store->remove("a");

    CHECK(!store->get("a").has_value());
    CHECK(store->values("a").empty());
    CHECK(!store->get_pending("a").has_value());
    // b is untouched
    CHECK(store->get("b").has_value());
    CHECK(store->list().size() == 1);
}

SBC_TEST(userscript_store_pending_and_apply) {
    auto dir = make_temp_dir("pending");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a"));  // version 1.0.0

    CHECK(!store->get_pending("a").has_value());
    json pending = {{"version", "2.0.0"}, {"source", "// new source v2"}, {"fetchedAt", 123456}};
    store->set_pending("a", pending);

    auto got = store->get_pending("a");
    CHECK(got.has_value());
    CHECK(got->value("version", "") == "2.0.0");

    // apply promotes source + version into the definition and clears pending.
    auto updated = store->apply_pending("a");
    CHECK(updated.has_value());
    CHECK(updated->value("version", "") == "2.0.0");
    CHECK(updated->value("source", "") == "// new source v2");
    CHECK(!store->get_pending("a").has_value());

    auto stored = store->get("a");
    CHECK(stored->value("source", "") == "// new source v2");
    CHECK(stored->value("version", "") == "2.0.0");
}

SBC_TEST(userscript_store_apply_without_pending) {
    auto dir = make_temp_dir("apply-none");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a"));
    CHECK(!store->apply_pending("a").has_value());  // no pending -> nullopt
    CHECK(!store->apply_pending("missing").has_value());
}

SBC_TEST(userscript_store_dismiss_pending) {
    auto dir = make_temp_dir("dismiss");
    auto store = UserscriptStore::open(dir.string());
    store->put(sample_script("a"));
    store->set_pending("a", json{{"version", "2.0.0"}, {"source", "x"}, {"fetchedAt", 1}});
    store->clear_pending("a");
    CHECK(!store->get_pending("a").has_value());
    // definition unchanged
    CHECK(store->get("a")->value("version", "") == "1.0.0");
}

SBC_TEST(userscript_store_ensure_builtin) {
    auto dir = make_temp_dir("builtin");
    auto store = UserscriptStore::open(dir.string());

    json spec = sample_script("builtin-x");
    spec["builtin"] = true;
    spec["enabled"] = false;

    // First call seeds it.
    store->ensure_builtin(spec);
    auto got = store->get("builtin-x");
    CHECK(got.has_value());
    CHECK(got->value("builtin", false));
    CHECK(!got->value("enabled", true));

    // A user edit (enable + change source) must survive a re-seed.
    json edited = *got;
    edited["enabled"] = true;
    edited["source"] = "// user edited";
    store->put(edited);

    store->ensure_builtin(spec);  // id already present -> no-op
    auto after = store->get("builtin-x");
    CHECK(after->value("enabled", false));
    CHECK(after->value("source", "") == "// user edited");
}

SBC_TEST(userscript_store_settings_persist) {
    auto dir = make_temp_dir("settings");
    {
        auto store = UserscriptStore::open(dir.string());
        // default before any write
        CHECK(store->get_settings().value("updateIntervalHours", -1) == 6);
        store->set_settings(json{{"updateIntervalHours", 12}});
    }
    {
        // reopen the DB and re-read
        auto store = UserscriptStore::open(dir.string());
        CHECK(store->get_settings().value("updateIntervalHours", -1) == 12);
    }
}
