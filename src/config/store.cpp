#include "config/store.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>

#include "common/error.hpp"
#include "config/json.hpp"
#include "platform/paths.hpp"

namespace sbc::config {

using nlohmann::ordered_json;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// reject_unknown throws the "json: unknown field" error expected by the API for
// any key not in the allowed set. Keys are matched case-sensitively.
void reject_unknown(const ordered_json& obj, std::initializer_list<const char*> allowed) {
    std::set<std::string> ok(allowed.begin(), allowed.end());
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!ok.count(it.key())) {
            throw Error("json: unknown field \"" + it.key() + "\"");
        }
    }
}

void strict_check(const ordered_json& root) {
    if (!root.is_object()) throw Error("config must be a JSON object");
    reject_unknown(root,
                   {"server", "mode", "upstream", "gameServer", "socks5Proxy", "localGameServer",
                    "gameServerStoragePath", "gameServerSettings", "cache", "package"});
    if (auto it = root.find("server"); it != root.end() && it->is_object()) {
        reject_unknown(*it, {"host", "port", "adminBasePath"});
    }
    if (auto it = root.find("gameServerSettings"); it != root.end() && it->is_object()) {
        reject_unknown(*it,
                       {"pingIntervalMs",      "pingTimeoutMs",         "maxPayloadBytes",
                        "messageRatePerSec",   "ipConnectionLimit",     "ipConnectionRatePerSec",
                        "accountCreatePerDay", "accountCreatePerHour",  "loginPaceMs",
                        "loginQueueThreshold", "pbkdf2Iterations",      "passwordResetThrottleMs",
                        "relationshipDelayMs", "serverInfoIntervalSec", "delayedFlushIntervalSec",
                        "searchMaxResults",    "roomLimitDefault",      "roomLimitMin",
                        "roomLimitMax",        "descriptionMaxLen",     "emailMaxLen",
                        "nameMaxLen",          "ownershipNotesMaxLen"});
    }
    if (auto it = root.find("cache"); it != root.end() && it->is_object()) {
        reject_unknown(*it, {"dir", "defaultTTLSeconds", "maxSizeBytes", "stores", "rules"});
        if (auto st = it->find("stores"); st != it->end() && st->is_array()) {
            for (const auto& s : *st) {
                if (s.is_object())
                    reject_unknown(s, {"name", "dir", "maxSizeBytes", "defaultTTLSeconds"});
            }
        }
        if (auto rl = it->find("rules"); rl != it->end() && rl->is_array()) {
            for (const auto& r : *rl) {
                if (r.is_object())
                    reject_unknown(
                        r, {"host", "pathPrefix", "pathPattern", "store", "bypass", "ttlSeconds",
                            "keyMode", "cacheControl", "forceCache", "version", "keyPattern",
                            "keyTemplate", "versionRevalidate"});
            }
        }
    }
    if (auto it = root.find("package"); it != root.end() && it->is_object()) {
        reject_unknown(*it, {"dir", "manifestUrl"});
    }
}

}  // namespace

Store Store::open(const std::string& path) {
    if (!path.empty()) return Store(std::filesystem::path(path));
    auto p = platform::user_config_dir() / "studio-bondage-club" / "config.json";
    return Store(std::move(p));
}

Config Store::load() {
    if (!std::filesystem::exists(path_)) {
        Config cfg = default_config();
        save(cfg);
        return cfg;
    }

    std::string text = read_file(path_);
    ordered_json doc;
    try {
        doc = ordered_json::parse(text);
    } catch (const std::exception& e) {
        throw Error("parse config " + path_.string() + ": " + e.what());
    }

    Config cfg = default_config();
    from_json(doc, cfg);
    cfg = normalize(std::move(cfg));
    cfg.validate();
    return cfg;
}

void Store::save(const Config& in) {
    Config cfg = normalize(in);
    cfg.validate();

    ordered_json doc = cfg;
    std::string text = doc.dump(2);
    text += '\n';

    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);

    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("write temp config: cannot open " + tmp.string());
        out << text;
        if (!out) throw Error("write temp config: write failed");
    }
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        throw Error("replace config: " + ec.message());
    }
}

Config parse_strict(const std::string& json_text) {
    ordered_json doc;
    try {
        doc = ordered_json::parse(json_text);
    } catch (const std::exception& e) {
        throw Error(std::string("invalid JSON: ") + e.what());
    }
    strict_check(doc);
    Config cfg = default_config();
    from_json(doc, cfg);
    return cfg;
}

}  // namespace sbc::config
