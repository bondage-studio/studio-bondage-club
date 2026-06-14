#include "common/http_util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace sbc {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

}  // namespace

std::string canonical_header_key(std::string_view key) {
    std::string out(key);
    bool upper_next = true;
    for (char& c : out) {
        if (upper_next) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            upper_next = false;
        } else {
            c = lower(c);
        }
        if (c == '-') upper_next = true;
    }
    return out;
}

std::string HeaderMap::get(std::string_view key) const {
    std::string ck = canonical_header_key(key);
    for (const auto& e : entries_) {
        if (e.first == ck) return e.second;
    }
    return {};
}

std::vector<std::string> HeaderMap::values(std::string_view key) const {
    std::string ck = canonical_header_key(key);
    std::vector<std::string> out;
    for (const auto& e : entries_) {
        if (e.first == ck) out.push_back(e.second);
    }
    return out;
}

bool HeaderMap::has(std::string_view key) const {
    std::string ck = canonical_header_key(key);
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.first == ck; });
}

void HeaderMap::set(std::string_view key, std::string_view value) {
    remove(key);
    add(key, value);
}

void HeaderMap::add(std::string_view key, std::string_view value) {
    entries_.emplace_back(canonical_header_key(key), std::string(value));
}

void HeaderMap::remove(std::string_view key) {
    std::string ck = canonical_header_key(key);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& e) { return e.first == ck; }),
                   entries_.end());
}

void HeaderMap::set_if_absent(std::string_view key, std::string_view value) {
    if (!has(key)) add(key, value);
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

bool header_contains_token(std::string_view header, std::string_view token) {
    std::size_t start = 0;
    while (start <= header.size()) {
        std::size_t comma = header.find(',', start);
        std::string_view part(header.data() + start,
                              (comma == std::string_view::npos ? header.size() : comma) - start);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
            part.remove_prefix(1);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
            part.remove_suffix(1);
        if (iequals(part, token)) return true;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return false;
}

bool is_hop_by_hop(std::string_view key) {
    static const std::array<const char*, 8> hop = {
        "Connection",          "Proxy-Connection", "Keep-Alive",     "Proxy-Authenticate",
        "Proxy-Authorization", "Te",               "Trailer",        "Transfer-Encoding"};
    // "Upgrade" is intentionally NOT stripped here; WebSocket upgrades are
    // handled explicitly before generic forwarding.
    for (const char* h : hop) {
        if (iequals(key, h)) return true;
    }
    return false;
}

void strip_hop_by_hop(HeaderMap& headers) {
    // Collect connection-listed tokens first.
    std::vector<std::string> conn_tokens;
    for (const auto& v : headers.values("Connection")) {
        std::size_t start = 0;
        while (start <= v.size()) {
            std::size_t comma = v.find(',', start);
            std::string token =
                trim(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!token.empty()) conn_tokens.push_back(token);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    std::vector<std::pair<std::string, std::string>> kept;
    for (const auto& e : headers.entries()) {
        if (is_hop_by_hop(e.first)) continue;
        bool listed = std::any_of(conn_tokens.begin(), conn_tokens.end(),
                                  [&](const std::string& t) { return iequals(t, e.first); });
        if (listed) continue;
        kept.push_back(e);
    }
    headers.clear();
    for (auto& e : kept) headers.add(e.first, e.second);
}

std::map<std::string, std::string> parse_cache_control(const std::vector<std::string>& values) {
    std::map<std::string, std::string> out;
    for (const auto& value : values) {
        std::size_t start = 0;
        while (start <= value.size()) {
            std::size_t comma = value.find(',', start);
            std::string part = trim(value.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!part.empty()) {
                std::size_t eq = part.find('=');
                if (eq == std::string::npos) {
                    std::string name = part;
                    std::transform(name.begin(), name.end(), name.begin(), lower);
                    out[name] = "";
                } else {
                    std::string name = trim(part.substr(0, eq));
                    std::transform(name.begin(), name.end(), name.begin(), lower);
                    std::string val = trim(part.substr(eq + 1));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                        val = val.substr(1, val.size() - 2);
                    }
                    out[name] = val;
                }
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    return out;
}

std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view s) {
    std::string str = trim(s);
    if (str.empty()) return std::nullopt;
    static const std::array<const char*, 3> formats = {
        "%a, %d %b %Y %H:%M:%S GMT",  // RFC 1123
        "%A, %d-%b-%y %H:%M:%S GMT",  // RFC 850
        "%a %b %e %H:%M:%S %Y"        // asctime
    };
    for (const char* fmt : formats) {
        std::tm tm{};
        // strptime is POSIX; on Windows we fall back to RFC1123 via std::get_time
        // through a stringstream (handled by the caller's platform build).
#if defined(_WIN32)
        std::istringstream iss(str);
        iss >> std::get_time(&tm, fmt);
        if (iss.fail()) continue;
#else
        if (::strptime(str.c_str(), fmt, &tm) == nullptr) continue;
#endif
#if defined(_WIN32)
        std::time_t t = _mkgmtime(&tm);
#else
        std::time_t t = timegm(&tm);
#endif
        if (t == static_cast<std::time_t>(-1)) continue;
        return std::chrono::system_clock::from_time_t(t);
    }
    return std::nullopt;
}

std::string format_http_date(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return buf;
}

}  // namespace sbc
