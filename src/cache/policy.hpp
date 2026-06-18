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

// upstream_expiry derives the stale-after time for a "default" cache entry (a route
// with no TTL and no cache_control override) by following the upstream response's
// freshness directives. Priority: s-maxage > max-age > Expires. A no-cache directive,
// the absence of any freshness directive, an invalid/past Expires, or a non-numeric/
// negative age all yield `now` (store but revalidate on the next request via
// ETag/Last-Modified). Storage itself stays gated by response_cacheable (no-store)
// and response_private.
std::optional<TimePoint> upstream_expiry(TimePoint now, const HeaderMap& resp_headers);

// response_private reports whether the response is marked Cache-Control: private,
// i.e. a shared cache must not store it. (no-store is handled by response_cacheable.)
bool response_private(const HeaderMap& resp_headers);

}  // namespace sbc::cache
