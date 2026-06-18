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
// JSON keys keep the existing config.json schema stable.
struct CacheRule {
    std::string host;
    std::string path_prefix;
    std::string path_pattern;

    std::string store;  // "" -> "default"
    bool bypass = false;
    std::optional<int> ttl_seconds;  // override TTL; nullopt -> store default
    std::string key_mode;
    std::string cache_control;
    bool force_cache = false;  // cache even when upstream says no-store
    // Cacheable HTTP status codes for this rule. Empty -> inherit the global
    // cache.cacheableStatusCodes (which itself defaults to {200, 204, 404}).
    std::vector<int> cacheable_status_codes;

    // Version-aware caching (all optional). `version` extracts a version label
    // from the target URL ("query:<name>" or "re:<regexp>"); a mismatch with the
    // stored entry's version forces an ETag revalidation. `key_pattern` +
    // `key_template` rewrite the upstream-relative path into a canonical cache
    // key (e.g. unifying Echo's GitHub-Pages and jsDelivr URL shapes), implying
    // path-based keying.
    std::string version;
    std::string key_pattern;
    std::string key_template;
    // version_revalidate selects how the version is used:
    //   false (default) -> immutable/content-addressed: the version is folded
    //     into the cache key, so each version is its own permanent entry and a
    //     new version is a normal miss (no ETag revalidation — pointless for a
    //     commit SHA / ?v= token whose content always differs).
    //   true -> the version is the freshness signal for a version-independent
    //     key (e.g. the game body's R-number): a mismatch forces an ETag
    //     revalidation so unchanged files 304 and reuse the cached body across
    //     releases (R129 -> R130).
    bool version_revalidate = false;
};

struct RouteAction {
    bool bypass = false;
    std::string store_name;       // "" means "default"
    std::chrono::seconds ttl{0};  // 0 means use the store's default
    std::string key_mode;         // "path" uses upstream-relative path
    std::string cache_control;
    bool force_cache = false;  // bypass ResponseCacheable checks
    // Per-rule cacheable status override; empty -> use the global default.
    std::vector<int> cacheable_status_codes;
    std::string version;
    std::string key_pattern;
    std::string key_template;
    bool version_revalidate = false;  // version = freshness signal vs key part
};

// Path-style glob (*, ?, [set]); '*' does not cross '/'.
bool glob_match(const std::string& pattern, const std::string& text);

// PolicyRouter evaluates an ordered CacheRule list to produce a RouteAction.
// Reads are near-lock-free via an atomically-swapped shared rule vector.
class PolicyRouter {
public:
    PolicyRouter() = default;
    explicit PolicyRouter(const std::vector<CacheRule>& rules);

    std::vector<CacheRule> rules() const;
    void update(const std::vector<CacheRule>& rules);

    RouteAction match(const Url& target, const Url& base) const;

private:
    struct CompiledRule {
        CacheRule rule;
        std::optional<std::regex> path_re;
        std::string path_glob;
    };

    static std::shared_ptr<const std::vector<CompiledRule>> compile(
        const std::vector<CacheRule>& rules);

    mutable std::shared_mutex mutex_;
    std::shared_ptr<const std::vector<CompiledRule>> compiled_ =
        std::make_shared<const std::vector<CompiledRule>>();
};

}  // namespace sbc::cache
