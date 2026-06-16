#include "server/userscript_store.hpp"

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

#include "common/error.hpp"

namespace sbc::server {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string script_key(const std::string& id) {
    return "s/" + id;
}
std::string pending_key(const std::string& id) {
    return "p/" + id;
}
std::string value_prefix(const std::string& id) {
    return "v/" + id + "/";
}
std::string value_key(const std::string& id, const std::string& key) {
    return value_prefix(id) + key;
}
constexpr const char* kSettingsKey = "meta/settings";
constexpr const char* kOptimizationKey = "meta/optimization";

bool starts_with(const leveldb::Slice& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

std::int64_t now_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

json default_settings() {
    return json{{"updateIntervalHours", 6}};
}

// Default optimization config: foreground ("default" rule) maps to the all-off
// "off" profile so the game is untouched while active; idle/background engage the
// progressively more aggressive "quiet"/"eco" profiles. Mirrors the schema the
// shell loader and the panel's Optimizations tab expect.
json default_optimization() {
    auto features = [](bool lazyCanvas, bool idleFpsThrottle, bool skipValidation, bool chatLogTrim,
                       bool tickRecorder) {
        return json{{"lazyCanvas", lazyCanvas},
                    {"idleFpsThrottle", idleFpsThrottle},
                    {"skipValidation", skipValidation},
                    {"chatLogTrim", chatLogTrim},
                    {"tickRecorder", tickRecorder}};
    };
    return json{
        {"enabled", true},
        {"profiles", json::array({
                         json{{"id", "off"},
                              {"name", "Off"},
                              {"features", features(false, false, false, false, false)}},
                         json{{"id", "eco"},
                              {"name", "Eco"},
                              {"features", features(true, true, true, true, false)}},
                         json{{"id", "quiet"},
                              {"name", "Quiet"},
                              {"features", features(true, false, false, true, false)}},
                     })},
        {"rules", json::array({
                      json{{"trigger", "background"}, {"profile", "eco"}},
                      json{{"trigger", "idle"}, {"idleSeconds", 30}, {"profile", "quiet"}},
                      json{{"trigger", "default"}, {"profile", "off"}},
                  })},
    };
}

}  // namespace

std::shared_ptr<UserscriptStore> UserscriptStore::open(const std::string& dir) {
    fs::path db_path = fs::path(dir) / "leveldb";
    std::error_code ec;
    fs::create_directories(db_path, ec);

    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, db_path.string(), &db);
    if (!status.ok()) {
        throw Error("open userscript store " + db_path.string() + ": " + status.ToString());
    }
    return std::shared_ptr<UserscriptStore>(new UserscriptStore(dir, db));
}

UserscriptStore::UserscriptStore(std::string dir, leveldb::DB* db)
    : dir_(std::move(dir)), db_(db) {}

UserscriptStore::~UserscriptStore() = default;

std::vector<json> UserscriptStore::list() {
    std::vector<json> scripts;
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
    for (it->Seek("s/"); it->Valid() && starts_with(it->key(), "s/"); it->Next()) {
        try {
            scripts.push_back(json::parse(it->value().ToString()));
        } catch (...) {
            // skip invalid entry
        }
    }
    std::stable_sort(scripts.begin(), scripts.end(), [](const json& a, const json& b) {
        return a.value("sortOrder", 0) < b.value("sortOrder", 0);
    });
    return scripts;
}

std::optional<json> UserscriptStore::get(const std::string& id) {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), script_key(id), &val);
    if (s.IsNotFound()) return std::nullopt;
    if (!s.ok()) throw Error("userscript get " + id + ": " + s.ToString());
    try {
        return json::parse(val);
    } catch (...) {
        return std::nullopt;
    }
}

void UserscriptStore::put(const json& script) {
    std::string id = script.value("id", "");
    if (id.empty()) throw Error("userscript put: missing id");
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, script_key(id), script.dump());
    if (!s.ok()) throw Error("userscript put " + id + ": " + s.ToString());
}

void UserscriptStore::ensure_builtin(const json& spec) {
    std::string id = spec.value("id", "");
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    std::string val;
    leveldb::Status g = db_->Get(leveldb::ReadOptions(), script_key(id), &val);
    if (g.ok()) return;  // already present — respect any user edits/toggles
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, script_key(id), spec.dump());
    if (!s.ok()) throw Error("userscript ensure_builtin " + id + ": " + s.ToString());
}

void UserscriptStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    leveldb::WriteBatch batch;
    batch.Delete(script_key(id));
    batch.Delete(pending_key(id));
    {
        std::string prefix = value_prefix(id);
        std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
        for (it->Seek(prefix); it->Valid() && starts_with(it->key(), prefix); it->Next()) {
            batch.Delete(it->key());
        }
    }
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("userscript remove " + id + ": " + s.ToString());
}

void UserscriptStore::reorder(const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mu_);
    leveldb::WriteBatch batch;
    int order = 0;
    for (const auto& id : ids) {
        std::string val;
        leveldb::Status s = db_->Get(leveldb::ReadOptions(), script_key(id), &val);
        if (!s.ok()) continue;  // skip missing ids
        json script;
        try {
            script = json::parse(val);
        } catch (...) {
            continue;
        }
        script["sortOrder"] = order++;
        batch.Put(script_key(id), script.dump());
    }
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("userscript reorder: " + s.ToString());
}

void UserscriptStore::set_pending(const std::string& id, const json& pending) {
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, pending_key(id), pending.dump());
    if (!s.ok()) throw Error("userscript set_pending " + id + ": " + s.ToString());
}

std::optional<json> UserscriptStore::get_pending(const std::string& id) {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), pending_key(id), &val);
    if (s.IsNotFound()) return std::nullopt;
    if (!s.ok()) throw Error("userscript get_pending " + id + ": " + s.ToString());
    try {
        return json::parse(val);
    } catch (...) {
        return std::nullopt;
    }
}

void UserscriptStore::clear_pending(const std::string& id) {
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Delete(wo, pending_key(id));
    if (!s.ok()) throw Error("userscript clear_pending " + id + ": " + s.ToString());
}

std::optional<json> UserscriptStore::apply_pending(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    std::string script_val;
    leveldb::Status ss = db_->Get(leveldb::ReadOptions(), script_key(id), &script_val);
    if (!ss.ok()) return std::nullopt;
    std::string pending_val;
    leveldb::Status ps = db_->Get(leveldb::ReadOptions(), pending_key(id), &pending_val);
    if (!ps.ok()) return std::nullopt;

    json script, pending;
    try {
        script = json::parse(script_val);
        pending = json::parse(pending_val);
    } catch (...) {
        return std::nullopt;
    }

    if (pending.contains("source")) script["source"] = pending["source"];
    if (pending.contains("version")) script["version"] = pending["version"];
    script["updatedAt"] = now_millis();

    leveldb::WriteBatch batch;
    batch.Put(script_key(id), script.dump());
    batch.Delete(pending_key(id));
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("userscript apply_pending " + id + ": " + s.ToString());
    return script;
}

json UserscriptStore::values(const std::string& id) {
    json out = json::object();
    std::string prefix = value_prefix(id);
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(prefix); it->Valid() && starts_with(it->key(), prefix); it->Next()) {
        std::string key = it->key().ToString().substr(prefix.size());
        try {
            out[key] = json::parse(it->value().ToString());
        } catch (...) {
            // skip invalid value
        }
    }
    return out;
}

void UserscriptStore::set_value(const std::string& id, const std::string& key,
                                const std::string& raw_json) {
    // Validate the body parses so we never persist a corrupt value.
    json parsed = json::parse(raw_json);
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, value_key(id, key), parsed.dump());
    if (!s.ok()) throw Error("userscript set_value " + id + "/" + key + ": " + s.ToString());
}

void UserscriptStore::del_value(const std::string& id, const std::string& key) {
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Delete(wo, value_key(id, key));
    if (!s.ok()) throw Error("userscript del_value " + id + "/" + key + ": " + s.ToString());
}

json UserscriptStore::get_settings() {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), kSettingsKey, &val);
    if (s.IsNotFound()) return default_settings();
    if (!s.ok()) throw Error(std::string("userscript get_settings: ") + s.ToString());
    try {
        json parsed = json::parse(val);
        // Backfill any missing defaults.
        json defaults = default_settings();
        for (auto& [k, v] : defaults.items()) {
            if (!parsed.contains(k)) parsed[k] = v;
        }
        return parsed;
    } catch (...) {
        return default_settings();
    }
}

void UserscriptStore::set_settings(const json& settings) {
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, kSettingsKey, settings.dump());
    if (!s.ok()) throw Error(std::string("userscript set_settings: ") + s.ToString());
}

json UserscriptStore::get_optimization() {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), kOptimizationKey, &val);
    if (s.IsNotFound()) return default_optimization();
    if (!s.ok()) throw Error(std::string("userscript get_optimization: ") + s.ToString());
    try {
        json parsed = json::parse(val);
        // Backfill top-level keys only; profiles/rules are owned wholesale by the
        // client so they are not merged field-by-field.
        json defaults = default_optimization();
        for (auto& [k, v] : defaults.items()) {
            if (!parsed.contains(k)) parsed[k] = v;
        }
        return parsed;
    } catch (...) {
        return default_optimization();
    }
}

void UserscriptStore::set_optimization(const json& optimization) {
    leveldb::WriteOptions wo;
    wo.sync = true;
    leveldb::Status s = db_->Put(wo, kOptimizationKey, optimization.dump());
    if (!s.ok()) throw Error(std::string("userscript set_optimization: ") + s.ToString());
}

}  // namespace sbc::server
