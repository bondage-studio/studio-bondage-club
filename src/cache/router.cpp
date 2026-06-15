#include "cache/router.hpp"

#include <cstddef>
#include <mutex>

#include "common/error.hpp"
#include "common/url.hpp"

namespace sbc::cache {

namespace {

// glob_segment matches a path glob against text where '*' matches any run of
// non-'/' characters. Supports ?, * and [set]/[^set] character classes.
bool glob_here(const std::string& pat, std::size_t pi, const std::string& text, std::size_t ti);

bool match_class(const std::string& pat, std::size_t& pi, char c, bool& valid) {
    ++pi;
    bool negated = false;
    if (pi < pat.size() && (pat[pi] == '^' || pat[pi] == '!')) {
        negated = true;
        ++pi;
    }
    bool matched = false;
    bool first = true;
    while (pi < pat.size() && (pat[pi] != ']' || first)) {
        first = false;
        char lo = pat[pi];
        if (lo == '\\' && pi + 1 < pat.size()) {
            ++pi;
            lo = pat[pi];
        }
        if (pi + 2 < pat.size() && pat[pi + 1] == '-' && pat[pi + 2] != ']') {
            char hi = pat[pi + 2];
            if (c >= lo && c <= hi) matched = true;
            pi += 3;
        } else {
            if (c == lo) matched = true;
            ++pi;
        }
    }
    if (pi >= pat.size() || pat[pi] != ']') {
        valid = false;
        return false;
    }
    ++pi;
    return negated ? !matched : matched;
}

bool glob_here(const std::string& pat, std::size_t pi, const std::string& text, std::size_t ti) {
    while (pi < pat.size()) {
        char p = pat[pi];
        if (p == '*') {
            ++pi;
            // '*' matches zero or more non-'/' chars; try longest-first backtracking.
            // First, attempt to match the rest at every position up to next '/'.
            for (std::size_t k = ti;; ++k) {
                if (glob_here(pat, pi, text, k)) return true;
                if (k >= text.size() || text[k] == '/') break;
            }
            return false;
        } else if (p == '?') {
            if (ti >= text.size() || text[ti] == '/') return false;
            ++pi;
            ++ti;
        } else if (p == '[') {
            if (ti >= text.size()) return false;
            bool valid = true;
            std::size_t save = pi;
            bool m = match_class(pat, pi, text[ti], valid);
            if (!valid) {
                pi = save;
                if (text[ti] != '[') return false;
                ++pi;
                ++ti;
                continue;
            }
            if (!m) return false;
            ++ti;
        } else if (p == '\\' && pi + 1 < pat.size()) {
            ++pi;
            if (ti >= text.size() || text[ti] != pat[pi]) return false;
            ++pi;
            ++ti;
        } else {
            if (ti >= text.size() || text[ti] != p) return false;
            ++pi;
            ++ti;
        }
    }
    return ti == text.size();
}

std::string base_name(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace

bool glob_match(const std::string& pattern, const std::string& text) {
    return glob_here(pattern, 0, text, 0);
}

PolicyRouter::PolicyRouter(const std::vector<CacheRule>& rules) { compiled_ = compile(rules); }

std::shared_ptr<const std::vector<PolicyRouter::CompiledRule>> PolicyRouter::compile(
    const std::vector<CacheRule>& rules) {
    auto out = std::make_shared<std::vector<CompiledRule>>();
    out->reserve(rules.size());
    for (std::size_t i = 0; i < rules.size(); ++i) {
        CompiledRule cr;
        cr.rule = rules[i];
        const std::string& pat = rules[i].path_pattern;
        if (!pat.empty()) {
            if (pat.rfind("re:", 0) == 0) {
                try {
                    cr.path_re.emplace(pat.substr(3), std::regex::ECMAScript);
                } catch (const std::regex_error& e) {
                    throw Error("rule[" + std::to_string(i) + "] pathPattern regexp: " +
                                e.what());
                }
            } else {
                cr.path_glob = pat;
            }
        }
        // Validate the version / key-rewrite regexps up front so bad config is
        // rejected at load time (extraction itself happens in cache/version.cpp).
        const std::string& ver = rules[i].version;
        if (ver.rfind("re:", 0) == 0) {
            try {
                std::regex(ver.substr(3), std::regex::ECMAScript);
            } catch (const std::regex_error& e) {
                throw Error("rule[" + std::to_string(i) + "] version regexp: " + e.what());
            }
        }
        const std::string& kp = rules[i].key_pattern;
        if (!kp.empty()) {
            try {
                std::regex(kp.rfind("re:", 0) == 0 ? kp.substr(3) : kp, std::regex::ECMAScript);
            } catch (const std::regex_error& e) {
                throw Error("rule[" + std::to_string(i) + "] keyPattern regexp: " + e.what());
            }
        }
        out->push_back(std::move(cr));
    }
    return out;
}

std::vector<CacheRule> PolicyRouter::rules() const {
    std::shared_lock lock(mutex_);
    auto snapshot = compiled_;
    lock.unlock();
    std::vector<CacheRule> out;
    out.reserve(snapshot->size());
    for (const auto& cr : *snapshot) out.push_back(cr.rule);
    return out;
}

void PolicyRouter::update(const std::vector<CacheRule>& rules) {
    auto next = compile(rules);
    std::unique_lock lock(mutex_);
    compiled_ = next;
}

RouteAction PolicyRouter::match(const Url& target, const Url& base) const {
    std::shared_ptr<const std::vector<CompiledRule>> snapshot;
    {
        std::shared_lock lock(mutex_);
        snapshot = compiled_;
    }

    std::string base_path = base.path();
    std::string tpath = target.path();
    std::string real_path = tpath;
    if (!base_path.empty() && tpath.rfind(base_path, 0) == 0) {
        real_path = tpath.substr(base_path.size());
    }
    if (real_path.empty() || real_path.front() != '/') real_path = "/" + real_path;

    std::string host = target.host();

    for (const auto& cr : *snapshot) {
        const CacheRule& r = cr.rule;
        if (!r.host.empty() && host != r.host) continue;
        if (!r.path_prefix.empty() && real_path.rfind(r.path_prefix, 0) != 0) continue;
        if (cr.path_re.has_value() && !std::regex_search(real_path, *cr.path_re)) continue;
        if (!cr.path_glob.empty()) {
            if (!glob_match(cr.path_glob, real_path) &&
                !glob_match(cr.path_glob, base_name(real_path))) {
                continue;
            }
        }
        RouteAction action;
        action.bypass = r.bypass;
        action.store_name = r.store;
        action.key_mode = r.key_mode;
        action.cache_control = r.cache_control;
        action.force_cache = r.force_cache;
        action.version = r.version;
        action.key_pattern = r.key_pattern;
        action.key_template = r.key_template;
        action.version_revalidate = r.version_revalidate;
        if (r.ttl_seconds.has_value()) {
            action.ttl = std::chrono::seconds(*r.ttl_seconds);
        }
        return action;
    }
    return RouteAction{};
}

}  // namespace sbc::cache
