#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <regex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace sbc {
class Url;
}

namespace sbc::cache {

// CacheRule describes one policy-routing rule. All non-empty conditions must
// match for the rule to apply; rules are evaluated in order, first match wins.
// JSON keys mirror the Go struct so existing config.json files stay compatible.
struct CacheRule {
    // Conditions
    std::string host;          // upstream hostname, e.g. "bondage-europe.com"
    std::string path_prefix;   // real path prefix, e.g. "/Assets/"
    std::string path_pattern;  // glob (e.g. "*.js") or "re:<regexp>"

    // Actions
    std::string store;                  // named store; "" -> "default"
    bool bypass = false;                // skip cache entirely
    std::optional<int> ttl_seconds;     // override TTL; nullopt -> store default
    std::string key_mode;               // "" | "url" (default) | "path"
    std::string cache_control;          // override Cache-Control on served responses
    bool force_cache = false;           // cache even when upstream says no-store
};

// RouteAction is the resolved policy for a single request URL.
struct RouteAction {
    bool bypass = false;
    std::string store_name;                  // "" means "default"
    std::chrono::seconds ttl{0};             // 0 means use the store's default
    std::string key_mode;                    // "path" uses upstream-relative path
    std::string cache_control;               // non-empty overrides response header
    bool force_cache = false;                // bypass ResponseCacheable checks
};

// glob_match matches a Go path.Match-style glob (*, ?, [set]) where '*' does not
// cross '/'. Returns false on malformed patterns (caller validates separately).
bool glob_match(const std::string& pattern, const std::string& text);

// PolicyRouter evaluates an ordered CacheRule list to produce a RouteAction.
// Reads are near-lock-free via an atomically-swapped shared rule vector.
class PolicyRouter {
public:
    PolicyRouter() = default;
    // Throws sbc::Error if any path_pattern is an invalid regex/glob.
    explicit PolicyRouter(const std::vector<CacheRule>& rules);

    std::vector<CacheRule> rules() const;
    // Atomically replaces the rule set. Throws on invalid pattern.
    void update(const std::vector<CacheRule>& rules);

    // match returns the first matching action; default-constructed if none.
    RouteAction match(const Url& target, const Url& base) const;

private:
    struct CompiledRule {
        CacheRule rule;
        std::optional<std::regex> path_re;  // set when path_pattern starts with "re:"
        std::string path_glob;              // set when path_pattern is a glob
    };

    static std::shared_ptr<const std::vector<CompiledRule>> compile(
        const std::vector<CacheRule>& rules);

    mutable std::shared_mutex mutex_;
    std::shared_ptr<const std::vector<CompiledRule>> compiled_ =
        std::make_shared<const std::vector<CompiledRule>>();
};

}  // namespace sbc::cache
