#include "cache/policy.hpp"

#include <algorithm>
#include <cctype>

namespace sbc::cache {

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool iequals(const std::string& a, const std::string& b) {
    return to_lower(a) == to_lower(b);
}

}  // namespace

bool request_forces_revalidation(const HeaderMap& req_headers) {
    auto cc = parse_cache_control(req_headers.values("Cache-Control"));
    if (cc.count("no-cache")) return true;
    auto it = cc.find("max-age");
    if (it != cc.end() && it->second == "0") return true;
    return iequals(req_headers.get("Pragma"), "no-cache");
}

bool response_cacheable(const std::string& method, const HeaderMap& req_headers, int status,
                        const HeaderMap& resp_headers, const std::set<int>& cacheable_statuses) {
    if (method != "GET") return false;
    if (!req_headers.get("Range").empty()) return false;
    if (!cacheable_statuses.count(status)) return false;
    if (!resp_headers.get("Content-Range").empty()) return false;
    if (!resp_headers.get("Content-Encoding").empty()) return false;
    if (!resp_headers.values("Set-Cookie").empty()) return false;

    auto cc = parse_cache_control(resp_headers.values("Cache-Control"));
    if (cc.count("no-store")) return false;

    for (const auto& value : resp_headers.values("Vary")) {
        std::size_t start = 0;
        while (start <= value.size()) {
            std::size_t comma = value.find(',', start);
            std::string field = to_lower(trim(value.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (!field.empty() && field != "accept-encoding") return false;
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return true;
}

std::optional<TimePoint> expiration(TimePoint now, std::chrono::seconds ttl) {
    if (ttl <= std::chrono::seconds(0)) return std::nullopt;
    return now + ttl;
}

}  // namespace sbc::cache
