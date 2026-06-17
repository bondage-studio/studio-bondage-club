#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "config/field_gen.hpp"
#include "config/json.hpp"
#include "server/config_scope.hpp"

// Builders that assemble ConfigScope objects from a declaration of which fields
// belong to a scope, instead of hand-writing the get/set lambdas. The slice
// shape is preserved exactly: scope slices always emit every member (so dirty
// detection and the updateScope RPC behave as before), and reads merge each key
// into the live config. Struct-backed scopes (cache, gamesettings, ...) reuse
// the struct's generated to_json/from_json directly.

namespace sbc::server {

// A single member inside a multi-field scope: emit writes its key into the slice,
// read merges its key back into the config.
struct ScopeField {
    std::function<void(nlohmann::ordered_json&, const config::Config&)> emit;
    std::function<void(config::Config&, const nlohmann::ordered_json&)> read;
};

// scalar_field binds one JSON key to a top-level Config member.
template <class T>
inline ScopeField scalar_field(const char* key, T config::Config::* mp) {
    return ScopeField{
        [key, mp](nlohmann::ordered_json& j, const config::Config& c) { j[key] = c.*mp; },
        [key, mp](config::Config& c, const nlohmann::ordered_json& j) {
            config::read_field(j, key, c.*mp);
        }};
}

// scalar_field_srv binds one JSON key to a nested Config::server member (the
// connection scope spans both top-level and server fields).
template <class T>
inline ScopeField scalar_field_srv(const char* key, T config::ServerConfig::* mp) {
    return ScopeField{
        [key, mp](nlohmann::ordered_json& j, const config::Config& c) { j[key] = c.server.*mp; },
        [key, mp](config::Config& c, const nlohmann::ordered_json& j) {
            config::read_field(j, key, c.server.*mp);
        }};
}

using TierFn = std::function<UpdateTier(const config::Config&, const config::Config&)>;

// make_scope builds a scope from an explicit field list (connection, gameserver,
// mode). make_struct_scope builds one backed by a whole Config sub-struct (cache,
// gamesettings, package, android), reusing its generated (de)serializers.
inline ConfigScope make_scope(std::string name, std::vector<ScopeField> fields, TierFn tier) {
    return ConfigScope{
        std::move(name),
        [fields](const config::Config& c) {
            nlohmann::ordered_json j = nlohmann::ordered_json::object();
            for (const auto& f : fields) f.emit(j, c);
            return j;
        },
        [fields](config::Config& c, const nlohmann::ordered_json& j) {
            for (const auto& f : fields) f.read(c, j);
        },
        std::move(tier)};
}

template <class Sub>
inline ConfigScope make_struct_scope(std::string name, Sub config::Config::* mp, TierFn tier) {
    return ConfigScope{
        std::move(name),
        [mp](const config::Config& c) { return nlohmann::ordered_json(c.*mp); },
        [mp](config::Config& c, const nlohmann::ordered_json& j) { config::from_json(j, c.*mp); },
        std::move(tier)};
}

inline UpdateTier tier_live(const config::Config&, const config::Config&) {
    return UpdateTier::Live;
}
inline UpdateTier tier_recreate(const config::Config&, const config::Config&) {
    return UpdateTier::Recreate;
}
inline UpdateTier tier_restart(const config::Config&, const config::Config&) {
    return UpdateTier::Restart;
}

}  // namespace sbc::server
