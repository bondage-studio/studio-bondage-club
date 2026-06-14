#include "cache/version.hpp"

#include <regex>

#include "common/error.hpp"
#include "common/url.hpp"

namespace sbc::cache {

namespace {

// query_param returns the raw (still percent-encoded) value of the first
// occurrence of `name` in a raw query string ("a=1&b=2"). Returns "" if absent.
std::string query_param(const std::string& raw_query, const std::string& name) {
    std::size_t pos = 0;
    while (pos <= raw_query.size()) {
        std::size_t amp = raw_query.find('&', pos);
        std::string pair = raw_query.substr(
            pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::size_t eq = pair.find('=');
        std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
        if (key == name) {
            return eq == std::string::npos ? std::string() : pair.substr(eq + 1);
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::string();
}

}  // namespace

std::string extract_version(const std::string& spec, const Url& target) {
    if (spec.empty()) return std::string();

    if (spec.rfind("query:", 0) == 0) {
        return query_param(target.query(), spec.substr(6));
    }

    if (spec.rfind("re:", 0) == 0) {
        std::regex re;
        try {
            re.assign(spec.substr(3), std::regex::ECMAScript);
        } catch (const std::regex_error& e) {
            throw Error(std::string("version regexp: ") + e.what());
        }
        std::string url = target.string();
        std::smatch m;
        if (std::regex_search(url, m, re)) {
            return m.size() > 1 ? m[1].str() : m[0].str();
        }
        return std::string();
    }

    // Unknown spec form: treat as no version.
    return std::string();
}

std::string rewrite_key_path(const std::string& real_path, const std::string& key_pattern,
                             const std::string& key_template) {
    if (key_pattern.empty()) return real_path;
    std::string pat = key_pattern.rfind("re:", 0) == 0 ? key_pattern.substr(3) : key_pattern;
    std::regex re;
    try {
        re.assign(pat, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
        throw Error(std::string("keyPattern regexp: ") + e.what());
    }
    return std::regex_replace(real_path, re, key_template);
}

}  // namespace sbc::cache
