#pragma once

#include <filesystem>
#include <string>

#include "config/config.hpp"

namespace sbc::config {

// Store persists Config as JSON. Load auto-creates a default file when missing;
// Save writes atomically (temp file + rename).
class Store {
public:
    static Store open(const std::string& path);

    const std::filesystem::path& path() const { return path_; }

    // load reads, normalizes and validates the config. Parsing is lenient:
    // unknown keys like "_comment" are ignored.
    Config load();

    void save(const Config& cfg);

private:
    explicit Store(std::filesystem::path path) : path_(std::move(path)) {}
    std::filesystem::path path_;
};

// parse_strict parses a JSON document into a Config, rejecting unknown fields.
// Throws sbc::Error / sbc::ValidationError on malformed input.
Config parse_strict(const std::string& json_text);

}  // namespace sbc::config
