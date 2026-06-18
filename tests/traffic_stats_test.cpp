#include "cache/traffic_stats.hpp"
#include "test_framework.hpp"

using namespace sbc::cache;

namespace {

std::int64_t count_of(const TrafficStats::HostStat& h, Outcome o) {
    return h.count[static_cast<std::size_t>(o)];
}
std::int64_t bytes_of(const TrafficStats::HostStat& h, Outcome o) {
    return h.bytes[static_cast<std::size_t>(o)];
}

const TrafficStats::HostStat* find_host(const TrafficStats::Snapshot& s, const std::string& host) {
    for (const auto& h : s.hosts)
        if (h.host == host) return &h;
    return nullptr;
}

const TrafficStats::ResourceStat* find_res(const TrafficStats::Detail& d, const std::string& url) {
    for (const auto& r : d.resources)
        if (r.url == url) return &r;
    return nullptr;
}

}  // namespace

SBC_TEST(traffic_classify_outcome) {
    CHECK(classify_outcome("HIT") == Outcome::Hit);
    CHECK(classify_outcome("STALE-HEAD") == Outcome::Hit);
    CHECK(classify_outcome("REVALIDATED") == Outcome::Revalidated);
    CHECK(classify_outcome("MISS") == Outcome::Miss);
    CHECK(classify_outcome("MISS-UNCACHED") == Outcome::Uncached);
    CHECK(classify_outcome("STALE") == Outcome::Stale);
    CHECK(classify_outcome("BYPASS-RULE") == Outcome::Bypass);
    CHECK(classify_outcome("BYPASS-METHOD") == Outcome::Bypass);
    CHECK(classify_outcome("something-else") == Outcome::Bypass);
}

SBC_TEST(traffic_records_per_host_counts_and_bytes) {
    TrafficStats t;
    t.record("cdn.example", "https://cdn.example/a.js", "HIT", 200, 100);
    t.record("cdn.example", "https://cdn.example/a.js", "HIT", 200, 50);
    t.record("cdn.example", "https://cdn.example/b.js", "MISS", 200, 200);
    t.record("mods.example", "https://mods.example/live", "BYPASS-RULE", 200, 0);

    auto snap = t.snapshot();
    CHECK(snap.hosts.size() == 2);

    const auto* cdn = find_host(snap, "cdn.example");
    CHECK(cdn != nullptr);
    CHECK(count_of(*cdn, Outcome::Hit) == 2);
    CHECK(count_of(*cdn, Outcome::Miss) == 1);
    CHECK(bytes_of(*cdn, Outcome::Hit) == 150);
    CHECK(bytes_of(*cdn, Outcome::Miss) == 200);
    CHECK(cdn->requests() == 3);

    const auto* mods = find_host(snap, "mods.example");
    CHECK(mods != nullptr);
    CHECK(count_of(*mods, Outcome::Bypass) == 1);

    // Aggregate row sums every host.
    CHECK(count_of(snap.total, Outcome::Hit) == 2);
    CHECK(count_of(snap.total, Outcome::Miss) == 1);
    CHECK(count_of(snap.total, Outcome::Bypass) == 1);
    CHECK(snap.total.requests() == 4);
}

SBC_TEST(traffic_detail_breaks_down_resources) {
    TrafficStats t;
    t.record("cdn.example", "https://cdn.example/a.js", "HIT", 200, 100);
    t.record("cdn.example", "https://cdn.example/a.js", "MISS", 200, 300);
    t.record("cdn.example", "https://cdn.example/b.css", "MISS", 404, 0);

    auto d = t.detail("cdn.example");
    CHECK(d.total.requests() == 3);
    CHECK(d.resources.size() == 2);

    const auto* a = find_res(d, "https://cdn.example/a.js");
    CHECK(a != nullptr);
    CHECK(a->count[static_cast<std::size_t>(Outcome::Hit)] == 1);
    CHECK(a->count[static_cast<std::size_t>(Outcome::Miss)] == 1);
    CHECK(a->requests() == 2);
    CHECK(a->last_status == 200);

    const auto* b = find_res(d, "https://cdn.example/b.css");
    CHECK(b != nullptr);
    CHECK(b->last_status == 404);

    // Resources sorted by request volume — a.js (2) before b.css (1).
    CHECK(d.resources.front().url == "https://cdn.example/a.js");

    // Unknown host yields an empty detail.
    auto none = t.detail("nope.example");
    CHECK(none.resources.empty());
    CHECK(none.total.requests() == 0);
}

SBC_TEST(traffic_sorted_by_requests_descending) {
    TrafficStats t;
    t.record("low.example", "https://low.example/x", "HIT", 200, 1);
    for (int i = 0; i < 5; ++i) t.record("high.example", "https://high.example/y", "MISS", 200, 10);

    auto snap = t.snapshot();
    CHECK(snap.hosts.size() == 2);
    CHECK(snap.hosts.front().host == "high.example");
    CHECK(snap.hosts.back().host == "low.example");
}

SBC_TEST(traffic_ignores_empty_host_and_clamps_negative_bytes) {
    TrafficStats t;
    t.record("", "https://x/y", "HIT", 200, 100);
    t.record("a.example", "https://a.example/y", "HIT", 200, -10);

    auto snap = t.snapshot();
    CHECK(snap.hosts.size() == 1);
    const auto* a = find_host(snap, "a.example");
    CHECK(a != nullptr);
    CHECK(bytes_of(*a, Outcome::Hit) == 0);
}

SBC_TEST(traffic_reset_clears_counts) {
    TrafficStats t;
    t.record("a.example", "https://a.example/y", "HIT", 200, 100);
    auto before = t.snapshot();
    CHECK(before.hosts.size() == 1);

    t.reset();
    auto after = t.snapshot();
    CHECK(after.hosts.empty());
    CHECK(after.total.requests() == 0);
    CHECK(after.since_ms >= before.since_ms);
}