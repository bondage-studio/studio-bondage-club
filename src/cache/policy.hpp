#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "cache/metadata.hpp"
#include "common/http_util.hpp"

namespace sbc::cache {

// request_forces_revalidation reports whether the request asks to bypass a
// fresh cache entry (Cache-Control: no-cache / max-age=0, or Pragma: no-cache).
bool request_forces_revalidation(const HeaderMap& req_headers);

// response_cacheable allows GET 200 responses without Range/Content-Range,
// Content-Encoding, Set-Cookie, Cache-Control: no-store, or Vary values other
// than Accept-Encoding.
bool response_cacheable(const std::string& method, const HeaderMap& req_headers, int status,
                        const HeaderMap& resp_headers);

// expiration returns the stale-after time. ttl <= 0 returns nullopt (never
// expires).
std::optional<TimePoint> expiration(TimePoint now, std::chrono::seconds ttl);

}  // namespace sbc::cache
