#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cache/metadata.hpp"  // Clock / TimePoint

// Max distinct resources (URLs) tracked per host in the traffic drill-down.
// Override at build time (e.g. -DSBC_TRAFFIC_MAX_RESOURCES_PER_HOST=50000) to
// trade memory for deeper per-file visibility. The default is generous because
// the drill-down's point is to name individual files for debugging, and the game
// serves many thousands of distinct asset URLs from one host — a small cap folds
// most of them (including the rarer uncacheable ones) into "(other)" and hides
// them. ~20k resources × ~200 B ≈ 4 MB for a fully-saturated host.
#ifndef SBC_TRAFFIC_MAX_RESOURCES_PER_HOST
#define SBC_TRAFFIC_MAX_RESOURCES_PER_HOST 20000
#endif

namespace sbc::cache {

// Outcome buckets a served request falls into, derived from the X-Studio-Cache
// status string. The enum values index the per-host count/byte arrays, so their
// order must stay in sync with kOutcomeNames in the .cpp.
enum class Outcome : std::size_t {
    Hit = 0,      // served from cache without contacting upstream (HIT / STALE-HEAD)
    Revalidated,  // 304 — cached body reused, only headers refetched (REVALIDATED)
    Miss,         // body fetched from upstream and cached (MISS)
    Uncached,     // body fetched from upstream but not cacheable (MISS-UNCACHED)
    Stale,        // stale cache served after an upstream failure (STALE)
    Bypass,       // not routed through the cache at all (BYPASS-*)
};
inline constexpr std::size_t kOutcomeCount = 6;

// Stable JSON key for each Outcome (index == enum value).
const char* outcome_name(Outcome o);

// classify_outcome maps an X-Studio-Cache status to its Outcome bucket. Unknown
// statuses fall back to Bypass.
Outcome classify_outcome(std::string_view cache_status);

// TrafficStats accumulates per-host (and per-resource) cache-outcome counts and
// served byte volumes since the last reset, so the panel can show live hit rates,
// the bandwidth a domain saves, and — on drill-down — the individual resources
// behind each outcome for debugging. Thread-safe: record() runs on any io thread
// as a request finishes; the snapshot/detail/reset readers come from the RPC
// layer.
class TrafficStats {
public:
    using Counters = std::array<std::int64_t, kOutcomeCount>;

    // One aggregate row (a host, or the all-hosts total).
    struct HostStat {
        std::string host;
        Counters count{};
        Counters bytes{};
        std::int64_t requests() const;  // sum over all outcomes
    };

    // One resource (URL) within a host, with its last seen status for debugging.
    struct ResourceStat {
        std::string url;
        Counters count{};
        Counters bytes{};
        int last_status = 0;     // last HTTP status code observed (0 if unknown)
        std::int64_t last_ms = 0;  // epoch-ms of the last request
        std::int64_t requests() const;
    };

    struct Snapshot {
        std::vector<HostStat> hosts;  // sorted by requests() descending
        HostStat total;               // aggregate across every host
        std::int64_t since_ms = 0;    // epoch-ms when the window opened (last reset)
        std::int64_t now_ms = 0;      // epoch-ms when the snapshot was taken
    };

    // Per-host drill-down: the host aggregate plus every tracked resource.
    struct Detail {
        HostStat total;                       // host aggregate (host name set)
        std::vector<ResourceStat> resources;  // sorted by requests() descending
        std::int64_t since_ms = 0;
        std::int64_t now_ms = 0;
    };

    // Records one served request. An empty host (e.g. a malformed cached URL) is
    // ignored; negative byte counts are clamped to zero. `status_code` <= 0 leaves
    // the resource's last status unchanged.
    void record(std::string_view host, std::string_view url, std::string_view cache_status,
                int status_code, std::int64_t bytes);

    // Lightweight aggregate view (no resources) — backs the live stream.
    Snapshot snapshot() const;

    // On-demand resource breakdown for one host. Empty resources when unknown.
    Detail detail(const std::string& host) const;

    void reset();

private:
    // The distinct-host set is bounded by the mod/CDN domains in play, and each
    // host keeps a bounded resource map, so a pathological workload can't grow the
    // maps without limit; once full, extra hosts/resources fold into a synthetic
    // "(other)" bucket.
    static constexpr std::size_t kMaxHosts = 512;
    static constexpr std::size_t kMaxResourcesPerHost = SBC_TRAFFIC_MAX_RESOURCES_PER_HOST;

    struct HostEntry {
        Counters count{};
        Counters bytes{};
        std::unordered_map<std::string, ResourceStat> resources;
    };

    mutable std::mutex mu_;
    std::unordered_map<std::string, HostEntry> by_host_;
    TimePoint since_ = Clock::now();
};

}  // namespace sbc::cache