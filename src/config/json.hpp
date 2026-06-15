#pragma once

#include <nlohmann/json.hpp>

#include "config/config.hpp"

// JSON (de)serialization for config structs. Declared here so any TU (store,
// API handlers) can serialize via nlohmann ADL. Implemented in config.cpp.
namespace sbc::config {

void to_json(nlohmann::ordered_json& j, const ServerConfig& s);
void from_json(const nlohmann::ordered_json& j, ServerConfig& s);
void to_json(nlohmann::ordered_json& j, const StoreConfig& s);
void from_json(const nlohmann::ordered_json& j, StoreConfig& s);
void to_json(nlohmann::ordered_json& j, const CacheConfig& c);
void from_json(const nlohmann::ordered_json& j, CacheConfig& c);
void to_json(nlohmann::ordered_json& j, const PackageConfig& p);
void from_json(const nlohmann::ordered_json& j, PackageConfig& p);
void to_json(nlohmann::ordered_json& j, const GameServerConfig& g);
void from_json(const nlohmann::ordered_json& j, GameServerConfig& g);
#if defined(__ANDROID__)
void to_json(nlohmann::ordered_json& j, const AndroidConfig& a);
void from_json(const nlohmann::ordered_json& j, AndroidConfig& a);
#endif
void to_json(nlohmann::ordered_json& j, const Config& c);
void from_json(const nlohmann::ordered_json& j, Config& c);

}  // namespace sbc::config
