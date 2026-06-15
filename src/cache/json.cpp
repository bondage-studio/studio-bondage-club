#include "cache/json.hpp"

#include "common/error.hpp"

namespace sbc::cache {

void to_json(nlohmann::ordered_json& j, const CacheRule& r) {
    j = nlohmann::ordered_json::object();
    if (!r.host.empty()) j["host"] = r.host;
    if (!r.path_prefix.empty()) j["pathPrefix"] = r.path_prefix;
    if (!r.path_pattern.empty()) j["pathPattern"] = r.path_pattern;
    if (!r.store.empty()) j["store"] = r.store;
    if (r.bypass) j["bypass"] = true;
    if (r.ttl_seconds.has_value()) j["ttlSeconds"] = *r.ttl_seconds;
    if (!r.key_mode.empty()) j["keyMode"] = r.key_mode;
    if (!r.cache_control.empty()) j["cacheControl"] = r.cache_control;
    if (r.force_cache) j["forceCache"] = true;
    if (!r.version.empty()) j["version"] = r.version;
    if (!r.key_pattern.empty()) j["keyPattern"] = r.key_pattern;
    if (!r.key_template.empty()) j["keyTemplate"] = r.key_template;
    if (r.version_revalidate) j["versionRevalidate"] = true;
}

void from_json(const nlohmann::ordered_json& j, CacheRule& r) {
    if (!j.is_object()) throw Error("cache rule must be a JSON object");
    r = CacheRule{};
    if (auto it = j.find("host"); it != j.end()) r.host = it->get<std::string>();
    if (auto it = j.find("pathPrefix"); it != j.end()) r.path_prefix = it->get<std::string>();
    if (auto it = j.find("pathPattern"); it != j.end()) r.path_pattern = it->get<std::string>();
    if (auto it = j.find("store"); it != j.end()) r.store = it->get<std::string>();
    if (auto it = j.find("bypass"); it != j.end()) r.bypass = it->get<bool>();
    if (auto it = j.find("ttlSeconds"); it != j.end() && !it->is_null())
        r.ttl_seconds = it->get<int>();
    if (auto it = j.find("keyMode"); it != j.end()) r.key_mode = it->get<std::string>();
    if (auto it = j.find("cacheControl"); it != j.end()) r.cache_control = it->get<std::string>();
    if (auto it = j.find("forceCache"); it != j.end()) r.force_cache = it->get<bool>();
    if (auto it = j.find("version"); it != j.end()) r.version = it->get<std::string>();
    if (auto it = j.find("keyPattern"); it != j.end()) r.key_pattern = it->get<std::string>();
    if (auto it = j.find("keyTemplate"); it != j.end()) r.key_template = it->get<std::string>();
    if (auto it = j.find("versionRevalidate"); it != j.end())
        r.version_revalidate = it->get<bool>();
}

}  // namespace sbc::cache
