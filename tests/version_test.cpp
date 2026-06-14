#include "cache/version.hpp"

#include "cache/key.hpp"
#include "common/url.hpp"
#include "test_framework.hpp"

using namespace sbc;
using namespace sbc::cache;

namespace {

Url url(const std::string& s) { return Url::parse(s); }

}  // namespace

SBC_TEST(version_extract_base_path) {
    auto t = url("https://bondage-europe.com/R129/BondageClub/CSS/Styles.css");
    CHECK(extract_version("re:/(R\\d+)/BondageClub/", t) == "R129");
}

SBC_TEST(version_extract_query) {
    auto t = url("https://sugarchain-studio.github.io/echo-clothing-ext/Manifest.json?v=1.2.3");
    CHECK(extract_version("query:v", t) == "1.2.3");
    CHECK(extract_version("query:v", url("https://x.test/a?b=1")).empty());
}

SBC_TEST(version_extract_jsdelivr_sha) {
    auto t = url(
        "https://cdn.jsdelivr.net/gh/SugarChain-Studio/echo-clothing-ext@abc1234/Manifest.json");
    CHECK(extract_version("re:@([^/]+)/", t) == "abc1234");
}

SBC_TEST(version_extract_empty_and_no_match) {
    auto t = url("https://example.com/a.js");
    CHECK(extract_version("", t).empty());
    CHECK(extract_version("re:/(R\\d+)/", t).empty());
    CHECK(extract_version("bogus-spec", t).empty());
}

SBC_TEST(version_invalid_regex_throws) {
    auto t = url("https://example.com/a.js");
    CHECK_THROWS(extract_version("re:([", t));
}

SBC_TEST(rewrite_key_unifies_echo_sources) {
    std::string gh = rewrite_key_path("/echo-clothing-ext/Manifest.json",
                                      "re:^/(echo-(?:clothing|activity)-ext)/(.*)$", "$1/$2");
    std::string jd = rewrite_key_path(
        "/gh/SugarChain-Studio/echo-clothing-ext@abc1234/Manifest.json",
        "re:^/gh/SugarChain-Studio/(echo-(?:clothing|activity)-ext)@[^/]+/(.*)$", "$1/$2");
    CHECK(gh == "echo-clothing-ext/Manifest.json");
    CHECK(gh == jd);
    CHECK(key_from_path(gh) == key_from_path(jd));
}

SBC_TEST(rewrite_key_passthrough_when_no_pattern) {
    CHECK(rewrite_key_path("/CSS/Styles.css", "", "") == "/CSS/Styles.css");
}
