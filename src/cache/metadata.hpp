#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "common/http_util.hpp"

namespace sbc::cache {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

// Metadata describes a cached response. The body itself lives under the
// "b/<key>" LevelDB key; this is the "m/<key>" value (serialized as JSON).
struct Metadata {
    std::string key;
    std::string url;
    int status_code = 0;
    HeaderMap header;
    TimePoint stored_at{};
    TimePoint last_accessed_at{};
    std::optional<TimePoint> expires_at;  // nullopt -> never expires
    std::int64_t body_size = 0;
    std::string body_sha256;
    std::string version;  // source version label ("" -> not version-tracked)

    // fresh reports whether the entry is still within its TTL.
    bool fresh(TimePoint now) const {
        if (!expires_at.has_value()) return true;  // never expires
        return now < *expires_at;
    }
    std::string etag() const { return header.get("ETag"); }
    std::string last_modified() const { return header.get("Last-Modified"); }
};

struct Stats {
    int entries = 0;
    std::int64_t bytes = 0;
};

}  // namespace sbc::cache
