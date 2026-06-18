#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>

#include "cache/metadata.hpp"
#include "common/http_util.hpp"

namespace sbc::cache {

// request_forces_revalidation reports whether the request asks to bypass a
// fresh cache entry (Cache-Control: no-cache / max-age=0, or Pragma: no-cache).
bool request_forces_revalidation(const HeaderMap& req_headers);

// response_cacheable allows GET responses whose status is in `cacheable_statuses`
// (e.g. {200, 204, 404}) and that carry no Range/Content-Range, Content-Encoding,
// Set-Cookie, Cache-Control: no-store, or Vary value other than Accept-Encoding.
bool response_cacheable(const std::string& method, const HeaderMap& req_headers, int status,
                        const HeaderMap& resp_headers, const std::set<int>& cacheable_statuses);

// expiration returns the stale-after time. ttl <= 0 returns nullopt (never
// expires).
std::optional<TimePoint> expiration(TimePoint now, std::chrono::seconds ttl);

}  // namespace sbc::cache
