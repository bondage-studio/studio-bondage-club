#pragma once

#include <filesystem>
#include <string>

#include "config/config.hpp"

namespace sbc::config {

// Store persists Config as JSON, mirroring Go's config.Store. Load auto-creates
// a default file when missing; Save writes atomically (temp file + rename).
class Store {
public:
    // open resolves the config path. An empty path defaults to
    // <user-config-dir>/studio-bondage-club/config.json.
    static Store open(const std::string& path);

    const std::filesystem::path& path() const { return path_; }

    // load reads, normalizes and validates the config. Parsing is lenient
    // (unknown keys like "_comment" are ignored), matching Go's Load.
    Config load();

    // save normalizes, validates, and atomically writes the config.
    void save(const Config& cfg);

private:
    explicit Store(std::filesystem::path path) : path_(std::move(path)) {}
    std::filesystem::path path_;
};

// parse_strict parses a JSON document into a Config, rejecting unknown fields
// (the analog of Go's decoder.DisallowUnknownFields used by PUT /api/config).
// Throws sbc::Error / sbc::ValidationError on malformed input.
Config parse_strict(const std::string& json_text);

}  // namespace sbc::config
