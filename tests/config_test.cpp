#include <nlohmann/json.hpp>

#include "cache/router.hpp"
#include "common/url.hpp"
#include "config/config.hpp"
#include "config/json.hpp"
#include "config/store.hpp"
#include "test_framework.hpp"

using namespace sbc;

namespace {

// A representative config including a "_comment" key (must be ignored on load)
// and the real rule set from run/config.json.
const char* kSampleConfig = R"JSON({
  "server": { "host": "127.0.0.1", "port": 8080, "adminBasePath": "/studio/" },
  "mode": "reverse_proxy_cache",
  "upstream": "https://bondage-europe.com/R129/BondageClub/",
  "gameServer": "https://bondage-club-server.herokuapp.com/",
  "socks5Proxy": "",
  "cache": {
    "dir": "/tmp/sbc-cache",
    "defaultTTLSeconds": 0,
    "maxSizeBytes": 5368709120,
    "stores": [
      { "name": "assets", "maxSizeBytes": 8589934592 },
      { "name": "mods", "maxSizeBytes": 2147483648 }
    ],
    "rules": [
      { "_comment": "ignored", "host": "claude.mods.bondage.club", "keyMode": "path",
        "forceCache": true, "cacheControl": "public, max-age=31536000, immutable", "store": "mods" },
      { "pathPrefix": "/Assets/", "pathPattern": "re:\\.(js|css|png)$", "keyMode": "path",
        "forceCache": true, "store": "assets",
        "version": "re:/(R\\d+)/BondageClub/", "versionRevalidate": true,
        "keyPattern": "re:^/(echo-(?:clothing|activity)-ext)/(.*)$", "keyTemplate": "$1/$2" }
    ]
  },
  "package": { "dir": "/tmp/sbc-packages", "manifestUrl": "" }
})JSON";

config::Config load_lenient(const std::string& text) {
    auto doc = nlohmann::ordered_json::parse(text);
    config::Config cfg = config::default_config();
    config::from_json(doc, cfg);
    cfg = config::normalize(std::move(cfg));
    cfg.validate();
    return cfg;
}

}  // namespace

SBC_TEST(default_config_is_valid) {
    auto cfg = config::default_config();
    cfg.validate();
    CHECK(cfg.server.port == 8080);
    CHECK(cfg.mode == config::kModeReverseProxy);
}

SBC_TEST(load_sample_ignores_comment_fields) {
    auto cfg = load_lenient(kSampleConfig);
    CHECK(cfg.cache.stores.size() == 2);
    CHECK(cfg.cache.rules.size() == 2);
    CHECK(cfg.cache.rules[0].host == "claude.mods.bondage.club");
    CHECK(cfg.cache.rules[0].key_mode == "path");
    CHECK(cfg.cache.rules[0].force_cache);
    CHECK(cfg.cache.rules[1].path_prefix == "/Assets/");
    CHECK(cfg.cache.rules[1].version == "re:/(R\\d+)/BondageClub/");
    CHECK(cfg.cache.rules[1].key_pattern == "re:^/(echo-(?:clothing|activity)-ext)/(.*)$");
    CHECK(cfg.cache.rules[1].key_template == "$1/$2");
    CHECK(cfg.cache.rules[1].version_revalidate);
}

SBC_TEST(emit_policy_omits_match_legacy_wire_format) {
    // StoreConfig: name always; dir omitted when empty; maxSizeBytes omitted when
    // 0; defaultTTLSeconds omitted when null. A default store carries only name.
    config::StoreConfig bare;
    bare.name = "assets";
    nlohmann::ordered_json jb = bare;
    CHECK(jb.dump() == R"({"name":"assets"})");

    config::StoreConfig full;
    full.name = "mods";
    full.dir = "/d";
    full.max_size_bytes = 5;
    full.default_ttl_seconds = 7;
    nlohmann::ordered_json jf = full;
    CHECK(jf.dump() == R"({"name":"mods","dir":"/d","maxSizeBytes":5,"defaultTTLSeconds":7})");

    // CacheConfig: scalar fields always emitted; empty stores/rules omitted.
    config::CacheConfig cc;
    cc.dir = "/c";
    nlohmann::ordered_json jc = cc;
    CHECK(jc.dump() == R"({"dir":"/c","defaultTTLSeconds":0,"maxSizeBytes":0})");

    // GameServerConfig: every field always emitted, in declared order.
    nlohmann::ordered_json jg = config::GameServerConfig{};
    CHECK(jg.contains("pingIntervalMs"));
    CHECK(jg.size() == 23);
    CHECK(jg.begin().key() == "pingIntervalMs");
    CHECK((--jg.end()).key() == "ownershipNotesMaxLen");

    // Config: top-level key order preserved.
    nlohmann::ordered_json jcfg = config::default_config();
    auto it = jcfg.begin();
    CHECK((it++).key() == "server");
    CHECK((it++).key() == "mode");
    CHECK((it++).key() == "upstream");
    CHECK((it++).key() == "gameServer");
    CHECK((it++).key() == "socks5Proxy");
    CHECK((it++).key() == "localGameServer");
    CHECK((it++).key() == "gameServerStoragePath");
    CHECK((it++).key() == "gameServerSettings");
    CHECK((it++).key() == "cache");
    CHECK((it++).key() == "package");
}

SBC_TEST(round_trip_preserves_fields) {
    auto cfg = load_lenient(kSampleConfig);
    nlohmann::ordered_json doc = cfg;
    std::string text = doc.dump(2);
    auto cfg2 = load_lenient(text);
    CHECK(cfg2.upstream == cfg.upstream);
    CHECK(cfg2.cache.stores.size() == cfg.cache.stores.size());
    CHECK(cfg2.cache.rules.size() == cfg.cache.rules.size());
    CHECK(cfg2.cache.rules[1].path_pattern == "re:\\.(js|css|png)$");
    CHECK(cfg2.cache.rules[1].version == "re:/(R\\d+)/BondageClub/");
    CHECK(cfg2.cache.rules[1].key_template == "$1/$2");
    CHECK(cfg2.cache.rules[1].version_revalidate);
}

SBC_TEST(parse_strict_rejects_unknown_field) {
    CHECK_THROWS(config::parse_strict(kSampleConfig));
    std::string clean = R"({"server":{"host":"127.0.0.1","port":9000,"adminBasePath":"/studio/"},
        "mode":"reverse_proxy_cache","upstream":"https://x.example/","gameServer":"https://y.example/",
        "cache":{"dir":"/tmp/c","defaultTTLSeconds":0,"maxSizeBytes":1}})";
    auto cfg = config::normalize(config::parse_strict(clean));
    cfg.validate();
    CHECK(cfg.server.port == 9000);
}

SBC_TEST(validate_rejects_bad_values) {
    auto base = load_lenient(kSampleConfig);

    auto bad_port = base;
    bad_port.server.port = 70000;
    CHECK_THROWS(bad_port.validate());

    auto bad_mode = base;
    bad_mode.mode = "nope";
    CHECK_THROWS(bad_mode.validate());

    auto dup_store = base;
    dup_store.cache.stores.push_back({"assets", "", 0, std::nullopt});
    CHECK_THROWS(dup_store.validate());

    auto unknown_store = base;
    cache::CacheRule r;
    r.store = "ghost";
    unknown_store.cache.rules.push_back(r);
    CHECK_THROWS(unknown_store.validate());

    auto bad_admin = base;
    bad_admin.server.admin_base_path = "studio";
    CHECK_THROWS(bad_admin.validate());
}

SBC_TEST(upstream_gets_trailing_slash) {
    Url u = config::parse_upstream("https://example.com/R129/BondageClub");
    CHECK(u.path() == "/R129/BondageClub/");
}

SBC_TEST(socks5_parsing) {
    CHECK(!config::parse_socks5_proxy("").has_value());
    auto u = config::parse_socks5_proxy("127.0.0.1:1080");
    CHECK(u.has_value());
    CHECK(u->scheme() == "socks5");
    CHECK(u->port() == 1080);
    CHECK_THROWS(config::parse_socks5_proxy("http://127.0.0.1:8080"));
    CHECK_THROWS(config::parse_socks5_proxy("127.0.0.1"));
}

SBC_TEST(glob_match_semantics) {
    CHECK(cache::glob_match("*.js", "foo.js"));
    CHECK(!cache::glob_match("*.js", "/a/foo.js"));
    CHECK(cache::glob_match("foo?.js", "foo1.js"));
    CHECK(!cache::glob_match("foo?.js", "foo.js"));
    CHECK(cache::glob_match("[abc]at", "bat"));
    CHECK(!cache::glob_match("[abc]at", "dat"));
}

SBC_TEST(game_settings_defaults_and_round_trip) {
    config::Config cfg = config::default_config();
    CHECK(cfg.game_server_settings.message_rate_per_sec == 20);
    CHECK(cfg.game_server_settings.relationship_delay_ms == 604800000);
    CHECK(cfg.game_server_settings.room_limit_max == 20);
    cfg.validate();

    cfg.game_server_settings.message_rate_per_sec = 7;
    cfg.game_server_settings.ping_interval_ms = 12345;
    nlohmann::ordered_json doc = cfg;
    config::Config back = config::default_config();
    config::from_json(doc, back);
    CHECK(back.game_server_settings.message_rate_per_sec == 7);
    CHECK(back.game_server_settings.ping_interval_ms == 12345);
    CHECK(back.game_server_settings.room_limit_default == 10);
}

SBC_TEST(game_settings_validation) {
    config::Config cfg = config::default_config();
    cfg.game_server_settings.room_limit_min = 15;
    cfg.game_server_settings.room_limit_max = 10;
    CHECK_THROWS(cfg.validate());

    config::Config cfg2 = config::default_config();
    cfg2.game_server_settings.message_rate_per_sec = 0;
    CHECK_THROWS(cfg2.validate());
}

SBC_TEST(game_settings_strict_parse) {
    const char* ok = R"({"server":{"host":"127.0.0.1","port":9000,"adminBasePath":"/studio/"},
        "mode":"reverse_proxy_cache","upstream":"https://x.example/","gameServer":"https://y.example/",
        "gameServerSettings":{"messageRatePerSec":9},
        "cache":{"dir":"/tmp/c","defaultTTLSeconds":0,"maxSizeBytes":1}})";
    auto cfg = config::normalize(config::parse_strict(ok));
    CHECK(cfg.game_server_settings.message_rate_per_sec == 9);

    const char* bad = R"({"server":{"host":"127.0.0.1","port":9000,"adminBasePath":"/studio/"},
        "mode":"reverse_proxy_cache","upstream":"https://x.example/","gameServer":"https://y.example/",
        "gameServerSettings":{"nope":1},
        "cache":{"dir":"/tmp/c","defaultTTLSeconds":0,"maxSizeBytes":1}})";
    CHECK_THROWS(config::parse_strict(bad));
}

SBC_TEST(game_storage_dir_defaults_and_override) {
    config::Config cfg = config::default_config();
    cfg.cache.dir = "/tmp/sbc-cache";

    cfg.game_server_storage_path.clear();
    CHECK(config::game_storage_dir(cfg) == "/tmp/sbc-cache/gameserver");

    cfg.game_server_storage_path = "/var/lib/sbc/accounts";
    CHECK(config::game_storage_dir(cfg) == "/var/lib/sbc/accounts");
    cfg.cache.dir = "/somewhere/else";
    CHECK(config::game_storage_dir(cfg) == "/var/lib/sbc/accounts");
}

SBC_TEST(game_storage_path_round_trip) {
    const char* text = R"({"server":{"host":"127.0.0.1","port":9000,"adminBasePath":"/studio/"},
        "mode":"reverse_proxy_cache","upstream":"https://x.example/","gameServer":"https://y.example/",
        "gameServerStoragePath":"  /data/accounts  ",
        "cache":{"dir":"/tmp/c","defaultTTLSeconds":0,"maxSizeBytes":1}})";
    auto cfg = load_lenient(text);
    CHECK(cfg.game_server_storage_path == "/data/accounts");

    nlohmann::ordered_json doc = cfg;
    auto back = load_lenient(doc.dump());
    CHECK(back.game_server_storage_path == "/data/accounts");
}
