#include "server/serve_content.hpp"

#include <cctype>
#include <cstdint>

namespace sbc::server {

namespace asio = boost::asio;

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string strip_weak(std::string tag) {
    if (tag.rfind("W/", 0) == 0) tag = tag.substr(2);
    return tag;
}

// etag_matches implements the If-None-Match / If-Range list comparison.
bool etag_matches(const std::string& header_value, const std::string& etag) {
    if (etag.empty()) return false;
    std::string target = strip_weak(etag);
    std::size_t start = 0;
    while (start <= header_value.size()) {
        std::size_t comma = header_value.find(',', start);
        std::string token = trim(header_value.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (token == "*") return true;
        if (strip_weak(token) == target) return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

// parse_single_range parses the first range of a "bytes=" Range header.
// Returns false if the header is malformed/unsupported; sets satisfiable=false
// for a syntactically valid but unsatisfiable range (→ 416).
bool parse_single_range(const std::string& header, std::int64_t total, std::int64_t& start,
                        std::int64_t& end, bool& satisfiable) {
    satisfiable = true;
    const std::string prefix = "bytes=";
    if (header.rfind(prefix, 0) != 0) return false;
    std::string spec = header.substr(prefix.size());
    std::size_t comma = spec.find(',');
    if (comma != std::string::npos) spec = spec.substr(0, comma);  // first range only
    spec = trim(spec);
    auto dash = spec.find('-');
    if (dash == std::string::npos) return false;
    std::string a = trim(spec.substr(0, dash));
    std::string b = trim(spec.substr(dash + 1));

    try {
        if (a.empty()) {
            if (b.empty()) return false;
            std::int64_t suffix = std::stoll(b);
            if (suffix <= 0) {
                satisfiable = false;
                return true;
            }
            if (suffix > total) suffix = total;
            start = total - suffix;
            end = total - 1;
        } else {
            start = std::stoll(a);
            end = b.empty() ? total - 1 : std::stoll(b);
            if (end >= total) end = total - 1;
        }
    } catch (...) {
        return false;
    }
    if (start < 0 || start >= total || start > end) {
        satisfiable = false;
        return true;
    }
    return true;
}

}  // namespace

asio::awaitable<void> serve_content(Request& req, ResponseWriter& w, HeaderMap headers,
                                    std::string body, std::optional<cache::TimePoint> mod_time) {
    std::string etag = headers.get("ETag");
    if (mod_time.has_value() && headers.get("Last-Modified").empty()) {
        headers.set("Last-Modified", format_http_date(*mod_time));
    }
    headers.set("Accept-Ranges", "bytes");

    // Conditional request handling (If-None-Match takes precedence).
    bool not_modified = false;
    std::string inm = req.headers.get("If-None-Match");
    if (!inm.empty()) {
        not_modified = etag_matches(inm, etag);
    } else {
        std::string ims = req.headers.get("If-Modified-Since");
        if (!ims.empty() && mod_time.has_value()) {
            if (auto since = parse_http_date(ims); since && *mod_time <= *since) not_modified = true;
        }
    }
    if (not_modified) {
        headers.remove("Content-Type");
        co_await w.send_header(304, std::move(headers), 0);
        co_await w.finish();
        co_return;
    }

    const std::int64_t total = static_cast<std::int64_t>(body.size());

    // Range handling (single range, GET only).
    std::string range = req.headers.get("Range");
    if (!range.empty() && req.is_get()) {
        bool range_applicable = true;
        std::string if_range = req.headers.get("If-Range");
        if (!if_range.empty()) {
            if (!if_range.empty() && (if_range.front() == '"' || if_range.rfind("W/", 0) == 0)) {
                range_applicable = etag_matches(if_range, etag);
            } else if (mod_time.has_value()) {
                auto d = parse_http_date(if_range);
                range_applicable = d.has_value() && *mod_time <= *d;
            } else {
                range_applicable = false;
            }
        }

        if (range_applicable) {
            std::int64_t start = 0, end = total - 1;
            bool satisfiable = true;
            if (parse_single_range(range, total, start, end, satisfiable)) {
                if (!satisfiable) {
                    headers.set("Content-Range", "bytes */" + std::to_string(total));
                    headers.remove("Content-Type");
                    co_await w.send_header(416, std::move(headers), 0);
                    co_await w.finish();
                    co_return;
                }
                std::int64_t len = end - start + 1;
                headers.set("Content-Range", "bytes " + std::to_string(start) + "-" +
                                                 std::to_string(end) + "/" +
                                                 std::to_string(total));
                co_await w.send_header(206, std::move(headers), len);
                if (!req.is_head() && len > 0) {
                    co_await w.write_chunk(
                        std::string_view(body).substr(static_cast<std::size_t>(start),
                                                      static_cast<std::size_t>(len)));
                }
                co_await w.finish();
                co_return;
            }
        }
    }

    // Full response.
    if (req.is_head()) {
        co_await w.send_header(200, std::move(headers), total);
        co_await w.finish();
    } else {
        co_await w.write_full(200, std::move(headers), std::move(body));
    }
}

}  // namespace sbc::server
