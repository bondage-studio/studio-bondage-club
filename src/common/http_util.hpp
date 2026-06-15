#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sbc {

// canonical_header_key converts a header name to MIME canonical form
// ("content-type" -> "Content-Type").
std::string canonical_header_key(std::string_view key);

// HeaderMap is a case-insensitive, multi-value, order-preserving header set.
class HeaderMap {
public:
    using Entry = std::pair<std::string, std::string>;  // canonical key, value

    std::string get(std::string_view key) const;
    std::vector<std::string> values(std::string_view key) const;
    bool has(std::string_view key) const;

    void set(std::string_view key, std::string_view value);
    void add(std::string_view key, std::string_view value);
    void remove(std::string_view key);

    void set_if_absent(std::string_view key, std::string_view value);

    const std::vector<Entry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

private:
    std::vector<Entry> entries_;
};

bool iequals(std::string_view a, std::string_view b);

// header_contains_token reports whether a comma-separated header value (e.g.
// "Connection: keep-alive, Upgrade") contains the given token, case-insensitively
// and ignoring surrounding whitespace.
bool header_contains_token(std::string_view header, std::string_view token);

// is_hop_by_hop reports whether a header must not be forwarded by a proxy
// (RFC 7230 6.1 plus common proxy-only headers).
bool is_hop_by_hop(std::string_view key);

// strip_hop_by_hop removes hop-by-hop headers (including any listed in
// Connection) in place.
void strip_hop_by_hop(HeaderMap& headers);

// parse_cache_control parses one or more Cache-Control header values into a
// lowercased directive map. Valueless directives map to "".
std::map<std::string, std::string> parse_cache_control(const std::vector<std::string>& values);

// HTTP-date helpers (RFC 1123 / RFC 850 / asctime on parse; RFC 1123 on format).
std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view s);
std::string format_http_date(std::chrono::system_clock::time_point tp);

}  // namespace sbc
