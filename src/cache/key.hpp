#pragma once

#include <string>
#include <string_view>

namespace sbc::cache {

// key returns the cache key for a full URL: lowercase hex SHA-256 of the URL
// string. Matches Go httpcache.Key.
std::string key(std::string_view raw_url);

// key_from_path derives a key from an upstream-relative path so cache entries
// survive upstream host/base changes. Matches Go httpcache.KeyFromPath.
std::string key_from_path(std::string_view real_path);

}  // namespace sbc::cache
