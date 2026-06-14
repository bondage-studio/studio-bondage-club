#include <atomic>
#include <filesystem>

#include "cache/key.hpp"
#include "cache/leveldb_store.hpp"
#include "cache/policy.hpp"
#include "common/http_util.hpp"
#include "test_framework.hpp"

using namespace sbc;
using namespace sbc::cache;
namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir(const std::string& tag) {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() /
                ("sbc-test-" + tag + "-" + std::to_string(counter.fetch_add(1)));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

Metadata sample_meta(const std::string& key) {
    Metadata m;
    m.key = key;
    m.url = "https://example.com/a.js";
    m.status_code = 200;
    m.header.set("Content-Type", "text/javascript");
    m.header.set("ETag", "\"abc\"");
    m.stored_at = Clock::now();
    return m;
}

}  // namespace

SBC_TEST(cache_key_is_sha256_hex) {
    auto k = cache::key("https://example.com/a.js");
    CHECK(k.size() == 64);
    CHECK(k == cache::key("https://example.com/a.js"));
    CHECK(k != cache::key("https://example.com/b.js"));
    // path key differs from url key for the same string content unless equal.
    CHECK(cache::key_from_path("/a.js") == cache::key("/a.js"));
}

SBC_TEST(policy_response_cacheable) {
    HeaderMap req;
    HeaderMap resp;
    resp.set("Content-Type", "text/javascript");
    CHECK(response_cacheable("GET", req, 200, resp));
    CHECK(!response_cacheable("POST", req, 200, resp));
    CHECK(!response_cacheable("GET", req, 302, resp));

    HeaderMap with_cookie = resp;
    with_cookie.add("Set-Cookie", "a=b");
    CHECK(!response_cacheable("GET", req, 200, with_cookie));

    HeaderMap no_store = resp;
    no_store.set("Cache-Control", "no-store");
    CHECK(!response_cacheable("GET", req, 200, no_store));

    HeaderMap vary_ae = resp;
    vary_ae.set("Vary", "Accept-Encoding");
    CHECK(response_cacheable("GET", req, 200, vary_ae));
    HeaderMap vary_cookie = resp;
    vary_cookie.set("Vary", "Cookie");
    CHECK(!response_cacheable("GET", req, 200, vary_cookie));

    HeaderMap range_req;
    range_req.set("Range", "bytes=0-10");
    CHECK(!response_cacheable("GET", range_req, 200, resp));
}

SBC_TEST(policy_forces_revalidation_and_expiration) {
    HeaderMap h;
    CHECK(!request_forces_revalidation(h));
    h.set("Cache-Control", "no-cache");
    CHECK(request_forces_revalidation(h));
    HeaderMap h2;
    h2.set("Pragma", "no-cache");
    CHECK(request_forces_revalidation(h2));

    auto now = Clock::now();
    CHECK(!expiration(now, std::chrono::seconds(0)).has_value());        // never expires
    CHECK(expiration(now, std::chrono::seconds(60)).has_value());
}

SBC_TEST(leveldb_store_roundtrip) {
    auto dir = make_temp_dir("store");
    auto store = LevelDbStore::open("default", dir.string());

    std::string key = cache::key("https://example.com/a.js");
    CHECK(!store->get(key).has_value());  // miss

    auto writer = store->new_writer(key);
    writer->write("hello ");
    writer->write("world");
    Metadata committed = writer->commit(sample_meta(key));
    CHECK(committed.body_size == 11);
    CHECK(committed.body_sha256.size() == 64);

    auto got = store->get(key);
    CHECK(got.has_value());
    CHECK(got->status_code == 200);
    CHECK(got->body_size == 11);
    CHECK(got->header.get("Content-Type") == "text/javascript");
    CHECK(got->etag() == "\"abc\"");

    CHECK(store->open_body(key) == "hello world");

    Stats s = store->stats();
    CHECK(s.entries == 1);
    CHECK(s.bytes == 11);

    // touch updates last-accessed without error.
    store->touch(key, Clock::now());

    store->clear();
    CHECK(store->stats().entries == 0);
    CHECK(!store->get(key).has_value());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

SBC_TEST(leveldb_store_lru_eviction) {
    auto dir = make_temp_dir("lru");
    auto store = LevelDbStore::open("default", dir.string());

    auto put = [&](const std::string& url, const std::string& body, Clock::time_point access) {
        std::string k = cache::key(url);
        auto w = store->new_writer(k);
        w->write(body);
        Metadata m = sample_meta(k);
        m.url = url;
        m.last_accessed_at = access;
        w->commit(m);
    };

    auto t0 = Clock::now();
    put("https://example.com/old", "AAAAAAAAAA", t0);                       // 10 bytes, oldest
    put("https://example.com/new", "BBBBBBBBBB", t0 + std::chrono::hours(1));  // 10 bytes, newest

    CHECK(store->stats().bytes == 20);
    store->enforce_max_size(10);  // must evict the older entry
    Stats s = store->stats();
    CHECK(s.entries == 1);
    CHECK(s.bytes == 10);
    CHECK(store->get(cache::key("https://example.com/new")).has_value());
    CHECK(!store->get(cache::key("https://example.com/old")).has_value());

    std::error_code ec;
    fs::remove_all(dir, ec);
}
