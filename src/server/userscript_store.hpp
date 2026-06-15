#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace leveldb {
class DB;
}

namespace sbc::server {

// UserscriptStore is a dedicated LevelDB key-value store holding all userscript
// state, fully decoupled from config.json. LevelDB writes are durable and atomic
// per call, so the frequent GM_setValue writes need no debounce. Key scheme:
//   "s/<id>"        -> script definition JSON
//   "p/<id>"        -> pending update JSON { version, source, fetchedAt }
//   "v/<id>/<key>"  -> one GM value (raw JSON)
//   "meta/settings" -> global settings { updateIntervalHours, ... }
//
// A single std::mutex serializes read-modify-write sequences. All calls block,
// so callers on the io threads bridge via net::run_blocking().
class UserscriptStore {
public:
    static std::shared_ptr<UserscriptStore> open(const std::string& dir);
    ~UserscriptStore();

    std::vector<nlohmann::json> list();  // sorted by sortOrder
    std::optional<nlohmann::json> get(const std::string& id);
    void put(const nlohmann::json& script);  // script must carry "id"
    // ensure_builtin seeds a default script only if its id is absent, so user
    // edits/toggles on a previously-seeded builtin are preserved across restarts.
    void ensure_builtin(const nlohmann::json& spec);
    void remove(const std::string& id);  // also drops p/<id> + v/<id>/ range
    // reorder rewrites sortOrder for the given ids in order; ids absent from the
    // store are ignored, scripts absent from `ids` keep their existing order.
    void reorder(const std::vector<std::string>& ids);

    // Pending updates (recorded by the background checker, applied on confirm).
    void set_pending(const std::string& id, const nlohmann::json& pending);
    std::optional<nlohmann::json> get_pending(const std::string& id);
    void clear_pending(const std::string& id);
    // apply_pending promotes p/<id> (source + version) into s/<id>, refreshes the
    // definition's updatedAt, and clears the pending record. Returns the updated
    // script, or nullopt if the script or pending record is missing.
    std::optional<nlohmann::json> apply_pending(const std::string& id);

    nlohmann::json values(const std::string& id);  // { key: value, ... }
    void set_value(const std::string& id, const std::string& key, const std::string& raw_json);
    void del_value(const std::string& id, const std::string& key);

    nlohmann::json get_settings();
    void set_settings(const nlohmann::json& settings);

private:
    UserscriptStore(std::string dir, leveldb::DB* db);

    std::string dir_;
    std::unique_ptr<leveldb::DB> db_;
    std::mutex mu_;
};

}  // namespace sbc::server
