#include "server/userscript_metadata.hpp"

#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace sbc::server {

namespace {

// Returns the substring between the // ==UserScript== and // ==/UserScript==
// markers (inclusive of neither), or the whole source when the close marker is
// absent but the open marker is present.
std::string metadata_block(const std::string& source) {
    static const std::regex open(R"(//\s*==UserScript==)");
    static const std::regex close(R"(//\s*==/UserScript==)");
    std::smatch m;
    if (!std::regex_search(source, m, open)) return std::string();
    std::string rest = source.substr(m.position(0) + m.length(0));
    std::smatch mc;
    if (std::regex_search(rest, mc, close)) return rest.substr(0, mc.position(0));
    return rest;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string parse_metadata_field(const std::string& source, const std::string& field) {
    std::string block = metadata_block(source);
    if (block.empty()) return std::string();
    // Match a line: optional spaces, //, spaces, @field, whitespace, value.
    std::regex line(R"(//\s*@)" + field + R"(\s+([^\r\n]+))");
    std::smatch m;
    if (std::regex_search(block, m, line)) return trim(m[1].str());
    return std::string();
}

bool version_newer(const std::string& candidate, const std::string& current) {
    if (candidate.empty()) return false;
    if (current.empty()) return true;
    if (candidate == current) return false;

    auto split = [](const std::string& v) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : v) {
            if (c == '.') {
                parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        parts.push_back(cur);
        return parts;
    };
    auto is_num = [](const std::string& s) {
        if (s.empty()) return false;
        for (char c : s)
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };

    std::vector<std::string> a = split(candidate);
    std::vector<std::string> b = split(current);
    std::size_t n = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::string pa = i < a.size() ? a[i] : "0";
        std::string pb = i < b.size() ? b[i] : "0";
        if (pa == pb) continue;
        if (is_num(pa) && is_num(pb)) {
            // Compare as integers (guard against overflow with long long).
            long long na = 0, nb = 0;
            try {
                na = std::stoll(pa);
                nb = std::stoll(pb);
            } catch (...) {
                return pa > pb;
            }
            if (na != nb) return na > nb;
        } else {
            return pa > pb;
        }
    }
    return false;
}

}  // namespace sbc::server
