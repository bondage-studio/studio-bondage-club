#include "cache/traffic_stats.hpp"

#include <algorithm>
#include <chrono>

namespace sbc::cache {

namespace {

std::int64_t to_epoch_ms(TimePoint tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

constexpr const char* kOutcomeNames[kOutcomeCount] = {
    "hit", "revalidated", "miss", "uncached", "stale", "bypass",
};

std::int64_t sum(const TrafficStats::Counters& c) {
    std::int64_t n = 0;
    for (auto v : c) n += v;
    return n;
}

}  // namespace

const char* outcome_name(Outcome o) {
    auto i = static_cast<std::size_t>(o);
    return i < kOutcomeCount ? kOutcomeNames[i] : "bypass";
}

Outcome classify_outcome(std::string_view s) {
    if (s == "HIT" || s == "STALE-HEAD") return Outcome::Hit;
    if (s == "REVALIDATED") return Outcome::Revalidated;
    if (s == "MISS") return Outcome::Miss;
    if (s == "MISS-UNCACHED") return Outcome::Uncached;
    if (s == "STALE") return Outcome::Stale;
    // BYPASS-METHOD / BYPASS-RULE / BYPASS-HEAD / BYPASS-RANGE and anything else.
    return Outcome::Bypass;
}

std::int64_t TrafficStats::HostStat::requests() const { return sum(count); }
std::int64_t TrafficStats::ResourceStat::requests() const { return sum(count); }

void TrafficStats::record(std::string_view host, std::string_view url, std::string_view cache_status,
                          int status_code, std::int64_t bytes) {
    if (host.empty()) return;
    if (bytes < 0) bytes = 0;
    const auto idx = static_cast<std::size_t>(classify_outcome(cache_status));
    const std::int64_t now_ms = to_epoch_ms(Clock::now());

    std::lock_guard<std::mutex> lock(mu_);
    std::string host_key(host);
    auto hit = by_host_.find(host_key);
    if (hit == by_host_.end()) {
        if (by_host_.size() >= kMaxHosts) host_key = "(other)";
        hit = by_host_.try_emplace(host_key).first;
    }
    HostEntry& he = hit->second;
    he.count[idx] += 1;
    he.bytes[idx] += bytes;

    std::string res_key(url);
    auto rit = he.resources.find(res_key);
    if (rit == he.resources.end()) {
        if (he.resources.size() >= kMaxResourcesPerHost) res_key = "(other)";
        rit = he.resources.try_emplace(res_key).first;
        if (rit->second.url.empty()) rit->second.url = res_key;
    }
    ResourceStat& rs = rit->second;
    rs.count[idx] += 1;
    rs.bytes[idx] += bytes;
    if (status_code > 0) rs.last_status = status_code;
    rs.last_ms = now_ms;
}

TrafficStats::Snapshot TrafficStats::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot snap;
    snap.hosts.reserve(by_host_.size());
    for (const auto& [host, he] : by_host_) {
        HostStat row;
        row.host = host;
        row.count = he.count;
        row.bytes = he.bytes;
        snap.hosts.push_back(std::move(row));
        for (std::size_t i = 0; i < kOutcomeCount; ++i) {
            snap.total.count[i] += he.count[i];
            snap.total.bytes[i] += he.bytes[i];
        }
    }
    std::sort(snap.hosts.begin(), snap.hosts.end(),
              [](const HostStat& a, const HostStat& b) { return a.requests() > b.requests(); });
    snap.since_ms = to_epoch_ms(since_);
    snap.now_ms = to_epoch_ms(Clock::now());
    return snap;
}

TrafficStats::Detail TrafficStats::detail(const std::string& host) const {
    std::lock_guard<std::mutex> lock(mu_);
    Detail d;
    d.total.host = host;
    d.since_ms = to_epoch_ms(since_);
    d.now_ms = to_epoch_ms(Clock::now());

    auto it = by_host_.find(host);
    if (it != by_host_.end()) {
        d.total.count = it->second.count;
        d.total.bytes = it->second.bytes;
        d.resources.reserve(it->second.resources.size());
        for (const auto& [url, rs] : it->second.resources) d.resources.push_back(rs);
        std::sort(d.resources.begin(), d.resources.end(),
                  [](const ResourceStat& a, const ResourceStat& b) {
                      return a.requests() > b.requests();
                  });
    }
    return d;
}

void TrafficStats::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    by_host_.clear();
    since_ = Clock::now();
}

}  // namespace sbc::cache