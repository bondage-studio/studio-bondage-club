#include "cache/router.hpp"
#include "common/url.hpp"
#include "test_framework.hpp"

using namespace sbc;
using namespace sbc::cache;

namespace {

CacheRule rule_host(const std::string& host, const std::string& store) {
    CacheRule r;
    r.host = host;
    r.store = store;
    r.key_mode = "path";
    r.force_cache = true;
    return r;
}

}  // namespace

SBC_TEST(router_matches_host_prefix_pattern) {
    CacheRule mods = rule_host("claude.mods.bondage.club", "mods");
    CacheRule assets;
    assets.path_prefix = "/Assets/";
    assets.path_pattern = "re:\\.(js|css)$";
    assets.key_mode = "path";
    assets.store = "assets";

    PolicyRouter router({mods, assets});
    Url base = Url::parse("https://bondage-europe.com/R129/BondageClub/");

    auto a1 = router.match(Url::parse("https://claude.mods.bondage.club/x/y.js"), base);
    CHECK(a1.store_name == "mods");
    CHECK(a1.key_mode == "path");
    CHECK(a1.force_cache);

    auto a2 = router.match(
        Url::parse("https://bondage-europe.com/R129/BondageClub/Assets/dir/a.js"), base);
    CHECK(a2.store_name == "assets");

    // Prefix matches but pattern does not -> falls through to default action.
    auto a3 = router.match(
        Url::parse("https://bondage-europe.com/R129/BondageClub/Assets/a.bin"), base);
    CHECK(a3.store_name.empty());
    CHECK(!a3.bypass);

    // No rule matches at all.
    auto a4 = router.match(Url::parse("https://other.example/whatever"), base);
    CHECK(a4.store_name.empty());
}

SBC_TEST(router_bypass_and_ttl) {
    CacheRule bypass;
    bypass.path_prefix = "/live/";
    bypass.bypass = true;
    CacheRule ttl;
    ttl.path_prefix = "/short/";
    ttl.ttl_seconds = 30;

    PolicyRouter router({bypass, ttl});
    Url base = Url::parse("https://h.example/");

    CHECK(router.match(Url::parse("https://h.example/live/x"), base).bypass);
    auto t = router.match(Url::parse("https://h.example/short/x"), base);
    CHECK(!t.bypass);
    CHECK(t.ttl == std::chrono::seconds(30));
}

SBC_TEST(router_update_swaps_rules) {
    PolicyRouter router{std::vector<CacheRule>{}};
    Url base = Url::parse("https://h.example/");
    CHECK(router.match(Url::parse("https://h.example/a"), base).store_name.empty());

    CacheRule r;
    r.path_prefix = "/a";
    r.store = "s";
    router.update({r});
    CHECK(router.match(Url::parse("https://h.example/a"), base).store_name == "s");
}
