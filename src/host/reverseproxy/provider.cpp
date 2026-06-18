#include "host/reverseproxy/provider.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <boost/asio/post.hpp>
#include <spdlog/spdlog.h>

#include "cache/key.hpp"
#include "cache/leveldb_store.hpp"
#include "cache/policy.hpp"
#include "cache/traffic_stats.hpp"
#include "cache/version.hpp"
#include "common/error.hpp"
#include "net/blocking_pool.hpp"
#include "net/content_decoding.hpp"
#include "net/http_client.hpp"
#include "net/io_runtime.hpp"
#include "net/socks5.hpp"
#include "net/tls.hpp"
#include "server/serve_content.hpp"

namespace sbc::host::reverseproxy {

namespace asio = boost::asio;
namespace fs = std::filesystem;
using cache::Clock;
using cache::Metadata;
using cache::TimePoint;

struct FetchResult {
    bool committed = false;
    std::shared_ptr<cache::Backend> store;
    Metadata meta;       // committed entry (read body from `store` via open_body)
    Metadata temp_meta;  // uncacheable/buffer-mode entry metadata
    std::string body;    // in-memory body for uncacheable/buffer-mode results
    std::string cache_status;
    std::string cache_control;
};

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

// Hop-by-hop set stripped by the reverse proxy. Includes Upgrade, excludes
// Proxy-Connection.
bool rp_hop(std::string_view key) {
    static const std::array hop = {"connection",          "keep-alive", "proxy-authenticate",
                                   "proxy-authorization", "te",         "trailer",
                                   "transfer-encoding",   "upgrade"};
    for (const char* h : hop) {
        if (iequals(key, h)) return true;
    }
    return false;
}

HeaderMap sanitize(const HeaderMap& src) {
    HeaderMap out;
    for (const auto& e : src.entries()) {
        if (!rp_hop(e.first)) out.add(e.first, e.second);
    }
    return out;
}

HeaderMap merge_headers(const HeaderMap& base, const HeaderMap& updates) {
    HeaderMap m;
    for (const auto& e : base.entries()) m.add(e.first, e.second);
    std::set<std::string> seen;
    for (const auto& e : updates.entries()) {
        if (!seen.count(e.first)) {
            m.remove(e.first);
            seen.insert(e.first);
        }
        m.add(e.first, e.second);
    }
    return m;
}

bool method_has_body(const std::string& method) {
    return method != "GET" && method != "HEAD" && method != "OPTIONS";
}

std::string single_joining_slash(const std::string& a, const std::string& b) {
    bool aslash = !a.empty() && a.back() == '/';
    bool bslash = !b.empty() && b.front() == '/';
    if (aslash && bslash) return a + b.substr(1);
    if (!aslash && !bslash) return a + "/" + b;
    return a + b;
}

// Like sanitize(), but also drops Content-Encoding, Content-Length, and Vary:
// cached and streamed bodies are the decoded identity representation, so the
// upstream's encoding/length headers no longer apply, and — because we always
// serve that single identity representation regardless of the client's
// Accept-Encoding — "Vary: Accept-Encoding" is inaccurate and only confuses
// browser/intermediary caches (policy.cpp guarantees Vary carries nothing else).
HeaderMap sanitize_decoded(const HeaderMap& src) {
    HeaderMap out;
    for (const auto& e : src.entries()) {
        if (rp_hop(e.first)) continue;
        if (iequals(e.first, "Content-Encoding") || iequals(e.first, "Content-Length")) continue;
        if (iequals(e.first, "Vary")) continue;
        out.add(e.first, e.second);
    }
    return out;
}

// Reports whether a Content-Encoding is one we decompress (so the body length
// changes). Identity / absent / unknown (raw passthrough) encodings keep the
// upstream body bytes and Content-Length unchanged.
bool encoding_transforms(const std::string& content_encoding) {
    std::size_t comma = content_encoding.find(',');
    std::string tok =
        content_encoding.substr(0, comma == std::string::npos ? content_encoding.size() : comma);
    std::size_t b = 0, e = tok.size();
    while (b < e && std::isspace(static_cast<unsigned char>(tok[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(tok[e - 1]))) --e;
    std::string_view t(tok.data() + b, e - b);
    return iequals(t, "gzip") || iequals(t, "x-gzip") || iequals(t, "deflate") || iequals(t, "br");
}

// Stale-after time for a freshly stored / revalidated entry. An explicit per-rule
// TTL wins; a curated cache_control override (immutable/long-lived rules) keeps the
// historical "never expires via TTL" behavior (its freshness is the override + any
// version revalidation); everything else is a default route that follows the
// upstream response's own freshness (cache::upstream_expiry).
std::optional<TimePoint> entry_expiry(TimePoint now, std::chrono::seconds ttl,
                                      const std::string& cache_control,
                                      const HeaderMap& resp_headers) {
    if (ttl > std::chrono::seconds(0)) return cache::expiration(now, ttl);
    if (!cache_control.empty()) return std::nullopt;
    return cache::upstream_expiry(now, resp_headers);
}

// A rule's cacheable-status override (if any) wins over the global default set.
std::set<int> resolve_cacheable_statuses(const std::set<int>& global,
                                         const std::vector<int>& rule_override) {
    if (rule_override.empty()) return global;
    return std::set<int>(rule_override.begin(), rule_override.end());
}

// TeeState is shared between the fetch's HeadHandler and its TeeSink. The
// HeadHandler decides whether to stream to the client and builds the decoder; the
// sink decodes each upstream chunk into identity bytes, accumulates them for the
// cache, and (in streaming mode) forwards them to the client.
struct TeeState {
    std::string body;                              // accumulated identity (or raw) body
    std::unique_ptr<net::ContentDecoder> decoder;  // null until the header arrives
    bool streaming = false;                        // actively writing decoded body to the client
    bool raw_passthrough = false;  // unsupported encoding: forward raw, do not cache
    bool client_gone = false;      // client write failed mid-stream; keep filling cache
};

// TeeSink decodes upstream body chunks to identity, buffers them, and streams to
// the client. A failed client write stops client delivery but keeps filling the
// cache so concurrent waiters (and the stored entry) still benefit.
class TeeSink : public net::BodySink {
public:
    TeeSink(std::shared_ptr<TeeState> st, server::ResponseWriter* w) : st_(std::move(st)), w_(w) {}

    asio::awaitable<void> on_chunk(std::string_view data) override {
        if (st_->raw_passthrough) {
            st_->body.append(data);
            co_await emit(data);
            co_return;
        }
        if (!st_->decoder) co_return;  // 304 / 5xx-with-cache: no body to decode
        std::string out = st_->decoder->decode(data);
        if (!out.empty()) {
            st_->body.append(out);
            co_await emit(out);
        }
    }

private:
    asio::awaitable<void> emit(std::string_view data) {
        if (!st_->streaming || w_ == nullptr || st_->client_gone) co_return;
        try {
            co_await w_->write_chunk(data);
        } catch (...) {
            st_->client_gone = true;  // drop client delivery; cache fill continues
        }
    }

    std::shared_ptr<TeeState> st_;
    server::ResponseWriter* w_;
};

class ResponseWriterSink : public net::BodySink {
public:
    ResponseWriterSink(server::ResponseWriter* w, bool skip) : w_(w), skip_(skip) {}
    asio::awaitable<void> on_chunk(std::string_view data) override {
        if (!skip_) co_await w_->write_chunk(data);
    }

private:
    server::ResponseWriter* w_;
    bool skip_;
};

std::shared_ptr<FetchResult> make_committed(std::shared_ptr<cache::Backend> store, Metadata meta,
                                            std::string status, std::string cache_control) {
    auto fr = std::make_shared<FetchResult>();
    fr->committed = true;
    fr->store = std::move(store);
    fr->meta = std::move(meta);
    fr->cache_status = std::move(status);
    fr->cache_control = std::move(cache_control);
    return fr;
}

void open_stores(const config::Config& cfg,
                 std::unordered_map<std::string, std::shared_ptr<cache::Backend>>& stores) {
    stores["default"] =
        cache::LevelDbStore::open("default", (fs::path(cfg.cache.dir) / "default").string());
    for (const auto& sc : cfg.cache.stores) {
        std::string dir = sc.dir.empty() ? (fs::path(cfg.cache.dir) / sc.name).string() : sc.dir;
        stores[sc.name] = cache::LevelDbStore::open(sc.name, dir);
    }
}

}  // namespace

std::shared_ptr<Provider> Provider::create(const config::Config& cfg,
                                           const host::ProviderContext& ctx) {
    auto p = std::shared_ptr<Provider>(new Provider());
    p->ctx_ = ctx;
    p->cache_dir_ = cfg.cache.dir;
    open_stores(cfg, p->stores_);
    p->router_ = std::make_shared<cache::PolicyRouter>(cfg.cache.rules);
    p->snap_ = p->build_snapshot(cfg);
    return p;
}

std::shared_ptr<Provider::Snapshot> Provider::build_snapshot(const config::Config& cfg) const {
    auto snap = std::make_shared<Snapshot>();
    snap->upstream = config::parse_upstream(cfg.upstream);
    snap->socks5_proxy = cfg.socks5_proxy;

    std::optional<net::Socks5Config> socks;
    if (auto u = config::parse_socks5_proxy(cfg.socks5_proxy)) {
        socks = net::socks5_config_from_url(*u);
    }
    snap->client = std::make_shared<net::HttpClient>(ctx_.io->executor(), *ctx_.tls, socks,
                                                     std::chrono::seconds(120));

    snap->default_ttl = std::chrono::seconds(cfg.cache.default_ttl_seconds);
    snap->default_max = cfg.cache.max_size_bytes;
    snap->store_ttl["default"] = snap->default_ttl;
    snap->store_max["default"] = snap->default_max;
    for (const auto& sc : cfg.cache.stores) {
        snap->store_ttl[sc.name] = sc.default_ttl_seconds.has_value()
                                       ? std::chrono::seconds(*sc.default_ttl_seconds)
                                       : snap->default_ttl;
        snap->store_max[sc.name] = sc.max_size_bytes > 0 ? sc.max_size_bytes : snap->default_max;
    }

    // Empty config list -> built-in default: cache OK (200), no-content (204) and
    // not-found (404). Caching 404s stops the game's probe requests for missing
    // asset variants (e.g. @nomap/...) from re-hitting upstream every time.
    if (cfg.cache.cacheable_status_codes.empty()) {
        snap->cacheable_statuses = {200, 204, 404};
    } else {
        snap->cacheable_statuses.insert(cfg.cache.cacheable_status_codes.begin(),
                                        cfg.cache.cacheable_status_codes.end());
    }
    snap->stale_if_error_seconds = cfg.cache.stale_if_error_seconds;
    return snap;
}

std::shared_ptr<const Provider::Snapshot> Provider::snapshot() const {
    std::shared_lock lock(mu_);
    return snap_;
}

void Provider::live_update(const config::Config& cfg) {
    router_->update(cfg.cache.rules);
    auto snap = build_snapshot(cfg);
    std::unique_lock lock(mu_);
    snap_ = snap;
}

std::vector<std::shared_ptr<cache::Backend>> Provider::all_stores() {
    std::shared_lock lock(mu_);
    std::vector<std::shared_ptr<cache::Backend>> out;
    out.reserve(stores_.size());
    for (auto& [name, backend] : stores_) out.push_back(backend);
    return out;
}

void Provider::close() {
    std::unique_lock lock(mu_);
    stores_.clear();
}

host::RuntimeStatus Provider::status() const {
    auto snap = snapshot();
    host::RuntimeStatus s;
    s.mode = config::kModeReverseProxy;
    if (snap) {
        s.upstream = snap->upstream.string();
    }
    s.cache_dir = cache_dir_;
    bool has_rules = !router_->rules().empty();
    bool has_socks = snap && !snap->socks5_proxy.empty();
    s.capabilities = {
        {"reverse-proxy-cache", "Reverse proxy cache", true,
         "Caches upstream static responses with ETag and Last-Modified revalidation."},
        {"stale-on-error", "Stale fallback", snap && snap->stale_if_error_seconds != 0,
         "Serves an existing cached file when the upstream request fails "
         "(window via cache.staleIfErrorSeconds)."},
        {"singleflight", "Concurrent request coalescing", true,
         "Only one upstream refresh runs for the same cache key at a time."},
        {"policy-routing", "Policy-based cache routing", has_rules,
         "Routes requests to named cache stores and applies per-rule TTL and bypass settings."},
        {"socks5-upstream-proxy", "SOCKS5 upstream proxy", has_socks,
         "Routes upstream requests through the configured SOCKS5 proxy."},
    };
    return s;
}

std::tuple<std::shared_ptr<cache::Backend>, std::chrono::seconds, std::int64_t>
Provider::resolve_store(const Snapshot& snap, const cache::RouteAction& action) const {
    std::string name = action.store_name.empty() ? "default" : action.store_name;
    auto it = stores_.find(name);
    std::shared_ptr<cache::Backend> store;
    if (it != stores_.end()) {
        store = it->second;
    } else {
        store = stores_.at("default");
        name = "default";
    }
    std::chrono::seconds ttl = action.ttl;
    if (ttl == std::chrono::seconds(0)) {
        auto tit = snap.store_ttl.find(name);
        ttl = tit != snap.store_ttl.end() ? tit->second : snap.default_ttl;
    }
    auto mit = snap.store_max.find(name);
    std::int64_t max_bytes = mit != snap.store_max.end() ? mit->second : snap.default_max;
    return {store, ttl, max_bytes};
}

std::string Provider::cache_key(const Url& upstream, const Url& target,
                                const cache::RouteAction& action,
                                const std::string& version) const {
    // For an immutable/content-addressed version (version_revalidate off), fold
    // the version into the key so each version is its own permanent entry. For a
    // revalidate version (the game's R-number) the key stays version-independent
    // and the version is bridged by ETag revalidation instead.
    std::string version_suffix;
    if (!version.empty() && !action.version_revalidate) version_suffix = "\x1f" + version;

    // A key_pattern rewrite (canonical key) or "path" key mode both key off the
    // upstream-relative path rather than the full URL.
    if (!action.key_pattern.empty() || action.key_mode == "path") {
        std::string tpath = target.path();
        std::string base = upstream.path();
        std::string real = tpath;
        if (!base.empty() && tpath.rfind(base, 0) == 0) real = tpath.substr(base.size());
        if (real.empty() || real.front() != '/') real = "/" + real;
        if (!action.key_pattern.empty()) {
            real = cache::rewrite_key_path(real, action.key_pattern, action.key_template);
        }
        return cache::key_from_path(real + version_suffix);
    }
    return cache::key(target.string() + version_suffix);
}

Url Provider::target_url(const Url& upstream, const server::Request& req) const {
    std::string req_enc_path = "/";
    if (auto u = Url::try_parse(req.target)) req_enc_path = u->encoded_path();
    std::string base = upstream.encoded_path();

    std::string joined;
    if (req_enc_path.empty() || req_enc_path == "/") {
        joined = base;
    } else {
        std::string rel = req_enc_path.front() == '/' ? req_enc_path.substr(1) : req_enc_path;
        joined = single_joining_slash(base, rel);
    }

    Url target = upstream;
    target.set_path(joined);
    target.set_query(req.raw_query);
    return target;
}

void Provider::schedule_touch(const std::shared_ptr<cache::Backend>& store, const std::string& key,
                              TimePoint now) {
    auto s = store;
    std::string k = key;
    asio::post(ctx_.blocking->pool(), [s, k, now]() { s->touch(k, now); });
}

void Provider::maybe_touch(const std::shared_ptr<cache::Backend>& store, const std::string& key,
                           const Metadata& meta, TimePoint now) {
    // Throttle access-time bookkeeping: an entry hit repeatedly would otherwise
    // produce a metadata write per request. Refresh only once the recorded access
    // time is older than the throttle window (a never-touched entry refreshes now).
    constexpr auto kTouchThrottle = std::chrono::seconds(60);
    TimePoint last = meta.last_accessed_at;
    if (last.time_since_epoch().count() != 0 && now - last < kTouchThrottle) return;
    schedule_touch(store, key, now);
}

void Provider::record_traffic(const std::string& url, const std::string& cache_status,
                              int status_code, std::int64_t bytes) const {
    if (ctx_.traffic == nullptr) return;
    auto u = Url::try_parse(url);
    if (!u || u->host().empty()) return;
    ctx_.traffic->record(u->host(), url, cache_status, status_code, bytes);
}

asio::awaitable<void> Provider::serve(server::Request& req, server::ResponseWriter& w) {
    auto snap = snapshot();
    Url target = target_url(snap->upstream, req);
    if (!req.is_get() && !req.is_head()) {
        co_await proxy_pass(req, w, *snap, target, "BYPASS-METHOD");
        co_return;
    }
    co_await serve_target(req, w, *snap, std::move(target));
}

asio::awaitable<void> Provider::serve_remote_http(server::Request& req, server::ResponseWriter& w,
                                                  const Url& target) {
    auto snap = snapshot();
    if (!req.is_get() && !req.is_head()) {
        co_await proxy_pass(req, w, *snap, target, "BYPASS-METHOD");
        co_return;
    }
    co_await serve_target(req, w, *snap, target);
}

asio::awaitable<void> Provider::serve_target(server::Request& req, server::ResponseWriter& w,
                                             const Snapshot& snap, Url target) {
    if (!req.is_get() && !req.is_head()) {
        co_await proxy_pass(req, w, snap, target, "BYPASS-METHOD");
        co_return;
    }
    cache::RouteAction action = router_->match(target, snap.upstream);
    if (action.bypass) {
        co_await proxy_pass(req, w, snap, target, "BYPASS-RULE");
        co_return;
    }

    auto [store, ttl, max_bytes] = resolve_store(snap, action);
    std::string version = cache::extract_version(action.version, target);
    std::string key = cache_key(snap.upstream, target, action, version);
    auto now = Clock::now();

    std::optional<Metadata> cached;
    try {
        cached =
            co_await net::run_blocking(*ctx_.blocking, [store, key]() { return store->get(key); });
    } catch (const std::exception& e) {
        spdlog::warn("ignore invalid cache entry key={} error={}", key, e.what());
    }

    // For a revalidate-version rule (game), the version tag — not TTL — is the
    // freshness signal: a mismatch (incl. a never-tagged entry, or a rollback)
    // forces a conditional GET that 304s and reuses the body across a bump
    // (R129 -> R130). Immutable versions instead live in the key, so a new
    // version is a plain miss and never reaches this check.
    bool version_mismatch =
        action.version_revalidate && !version.empty() && cached && cached->version != version;
    bool force_reval = cache::request_forces_revalidation(req.headers) || version_mismatch;
    if (cached && cached->fresh(now) && !force_reval) {
        maybe_touch(store, key, *cached, now);
        co_await serve_entry(req, w, store, *cached, "HIT", action.cache_control, false);
        co_return;
    }

    if (req.is_head()) {
        if (cached && !force_reval) {
            maybe_touch(store, key, *cached, now);
            co_await serve_entry(req, w, store, *cached, "STALE-HEAD", action.cache_control, false);
            co_return;
        }
        co_await proxy_pass(req, w, snap, target, "BYPASS-HEAD");
        co_return;
    }

    if (!req.headers.get("Range").empty() && !cached) {
        co_await proxy_pass(req, w, snap, target, "BYPASS-RANGE");
        co_return;
    }

    std::shared_ptr<FetchResult> result;
    std::string fetch_error;
    bool failed = false;
    try {
        result = co_await flights_.do_call(
            key,
            [this, &snap, target, key, store, ttl, max_bytes, action, force_reval, version, &req,
             &w]() -> asio::awaitable<std::shared_ptr<FetchResult>> {
                std::optional<Metadata> refreshed;
                try {
                    refreshed = co_await net::run_blocking(
                        *ctx_.blocking, [store, key]() { return store->get(key); });
                } catch (...) {
                    refreshed = std::nullopt;
                }
                bool reval = force_reval || (action.version_revalidate && !version.empty() &&
                                             refreshed && refreshed->version != version);
                if (refreshed && refreshed->fresh(Clock::now()) && !reval) {
                    co_return make_committed(store, *refreshed, "HIT", action.cache_control);
                }
                // Only revalidate-mode versions are recorded in metadata (and so
                // surface in the versions/expire UI); immutable versions live in
                // the key and need no tag. The leader passes &w so fetch() streams
                // the response straight to this client while filling the cache.
                co_return co_await fetch(
                    snap, target, key, store, ttl, max_bytes, action.force_cache,
                    resolve_cacheable_statuses(snap.cacheable_statuses,
                                               action.cacheable_status_codes),
                    action.cache_control, action.version_revalidate ? version : std::string(),
                    refreshed, req, &w);
            });
    } catch (const std::exception& e) {
        fetch_error = e.what();
        failed = true;
    }

    if (failed) {
        // If the leader already began streaming to this client, the header is sent
        // and we cannot fall back to stale/502 — just drop the connection.
        if (w.header_sent()) co_return;
        if (cached && cache::stale_if_error_servable(cached->expires_at, Clock::now(),
                                                     snap.stale_if_error_seconds)) {
            spdlog::warn("serving stale cache after upstream failure url={} error={}",
                         target.string(), fetch_error);
            co_await serve_entry(req, w, store, *cached, "STALE", action.cache_control, true);
            co_return;
        }
        HeaderMap h;
        h.set("Content-Type", "text/plain; charset=utf-8");
        co_await w.write_full(502, std::move(h), "upstream request failed: " + fetch_error + "\n");
        co_return;
    }

    // The singleflight leader already streamed the full response inline (header
    // sent by fetch()). Followers — and the leader on a 304 revalidate, which has
    // no streamed body — serve from the committed/uncacheable result here.
    if (w.header_sent()) co_return;
    co_await serve_result(req, w, *result);
}

asio::awaitable<std::shared_ptr<FetchResult>> Provider::fetch(
    const Snapshot& snap, Url target, std::string key, std::shared_ptr<cache::Backend> store,
    std::chrono::seconds ttl, std::int64_t max_bytes, bool force_cache,
    std::set<int> cacheable_statuses, std::string cache_control, std::string version,
    std::optional<Metadata> cached, server::Request& req, server::ResponseWriter* w) {
    net::ClientRequest creq;
    creq.method = "GET";
    creq.target = target;
    for (const auto& e : req.headers.entries()) {
        if (!rp_hop(e.first) && !iequals(e.first, "Range")) creq.headers.add(e.first, e.second);
    }
    // Request what we can decode. We cache + serve a single identity
    // representation, so this is independent of the downstream client's
    // Accept-Encoding — the win is a smaller/faster upstream transfer.
    creq.headers.set("Accept-Encoding", "gzip, br");
    if (cached) {
        if (auto et = cached->etag(); !et.empty()) creq.headers.set("If-None-Match", et);
        if (auto lm = cached->last_modified(); !lm.empty())
            creq.headers.set("If-Modified-Since", lm);
    }

    bool had_cached = cached.has_value();
    auto st = std::make_shared<TeeState>();

    // Runs once the upstream header arrives: decides streaming and sets up the
    // decoder. 304 (our conditional GET matched) and 5xx-with-cache are resolved
    // after the body completes (revalidate-from-cache / stale fallback), so they
    // neither stream nor decode here.
    net::HeadHandler on_head = [st, w, had_cached, cache_control](
                                   const net::ClientResponse& resp) -> asio::awaitable<void> {
        if (resp.status == 304) co_return;
        if (had_cached && resp.status >= 500) co_return;

        std::string ce = resp.headers.get("Content-Encoding");
        st->decoder = net::make_decoder(ce);
        if (!st->decoder) st->raw_passthrough = true;  // unknown encoding -> raw, uncached

        if (w == nullptr) co_return;  // buffer mode (homepage): collect body only

        // When we decompress (gzip/br), the decoded length is unknown until the
        // body completes, so a live stream would have to be length-less/chunked —
        // which browsers are reluctant to cache. Defer those to the post-flight
        // cache serve (serve_entry), which emits a proper Content-Length. Such
        // bodies are text and small, so the streaming TTFB cost is negligible.
        if (encoding_transforms(ce)) co_return;

        // Identity / raw passthrough: the body bytes are unchanged, so stream live
        // while preserving the upstream Content-Length. This keeps the streaming
        // TTFB win for the large assets (images) and a cacheable response.
        HeaderMap h;
        for (const auto& e : resp.headers.entries()) {
            if (rp_hop(e.first)) continue;
            // Identity is served regardless of Accept-Encoding, so drop the
            // inaccurate Vary (keep it for a raw passthrough, which is encoded).
            if (!st->raw_passthrough && iequals(e.first, "Vary")) continue;
            h.add(e.first, e.second);  // Content-Length is re-applied by send_header below
        }
        if (!cache_control.empty()) {
            h.set("Cache-Control", cache_control);
            h.remove("Expires");
            h.remove("Pragma");
        }
        h.set_if_absent("Accept-Ranges", "bytes");
        h.set("X-Studio-Cache", "MISS");
        std::optional<std::int64_t> clen;
        if (auto cl = resp.headers.get("Content-Length"); !cl.empty()) {
            try {
                clen = std::stoll(cl);
            } catch (...) {
            }
        }
        st->streaming = true;
        try {
            co_await w->send_header(resp.status, std::move(h), clen);
        } catch (...) {
            // Client vanished while sending the header: keep filling the cache so
            // concurrent waiters still benefit; just stop delivering to this client.
            st->streaming = false;
            st->client_gone = true;
        }
    };

    TeeSink sink(st, w);
    net::ClientResponse resp;
    std::exception_ptr ferr;
    try {
        resp = co_await snap.client->fetch(creq, on_head, sink);
    } catch (...) {
        ferr = std::current_exception();
    }
    if (ferr) std::rethrow_exception(ferr);

    // Flush the decoder's trailing output (no-op for a raw passthrough).
    if (st->decoder) {
        std::string tail = st->decoder->finish();
        if (!tail.empty()) {
            st->body.append(tail);
            if (st->streaming && w && !st->client_gone) {
                try {
                    co_await w->write_chunk(tail);
                } catch (...) {
                    st->client_gone = true;
                }
            }
        }
    }

    auto now = Clock::now();

    // 304: our conditional GET matched — refresh metadata, reuse the cached body.
    if (resp.status == 304 && cached) {
        HeaderMap merged = merge_headers(cached->header, sanitize_decoded(resp.headers));
        auto updated = co_await net::run_blocking(
            *ctx_.blocking, [store, key, merged, now, ttl, version, cache_control]() {
                return store->update_metadata(key, [&](Metadata m) {
                    m.header = merged;
                    m.stored_at = now;
                    // Refresh freshness from the just-revalidated headers (a 304 may
                    // carry updated Cache-Control/Expires), same policy as a store.
                    m.expires_at = entry_expiry(now, ttl, cache_control, merged);
                    if (!version.empty()) m.version = version;
                    return m;
                });
            });
        Metadata meta = updated ? *updated : *cached;
        co_return make_committed(store, meta, "REVALIDATED", cache_control);
    }

    if (cached && resp.status >= 500) {
        throw Error("upstream returned status " + std::to_string(resp.status));
    }

    Metadata meta;
    meta.key = key;
    meta.url = target.string();
    meta.status_code = resp.status;
    // Raw passthrough keeps the (compressed) body + its encoding header; the normal
    // path stores the decoded identity body with the encoding/length stripped.
    meta.header = st->raw_passthrough ? sanitize(resp.headers) : sanitize_decoded(resp.headers);
    meta.stored_at = now;
    meta.expires_at = entry_expiry(now, ttl, cache_control, meta.header);
    meta.version = version;

    // A default route (no forced caching) must honor Cache-Control: private — a
    // shared cache may not store it. force_cache and curated rules bypass this.
    bool cacheable =
        !st->raw_passthrough && ((cache::response_cacheable("GET", creq.headers, resp.status,
                                                            meta.header, cacheable_statuses) &&
                                  !cache::response_private(meta.header)) ||
                                 (force_cache && cacheable_statuses.count(resp.status)));
    if (cacheable) {
        Metadata committed = co_await net::run_blocking(
            *ctx_.blocking, [store, key, body = std::move(st->body), meta]() mutable {
                return store->put(key, std::move(body), std::move(meta));
            });
        co_await net::run_blocking(*ctx_.blocking,
                                   [store, max_bytes]() { store->enforce_max_size(max_bytes); });
        if (st->streaming && w && !st->client_gone) {
            // The body is fully delivered; a client that drops before the final
            // (chunked) terminator must not surface as a handler error.
            try {
                co_await w->finish();
            } catch (...) {
            }
            record_traffic(target.string(), "MISS", resp.status, committed.body_size);
        }
        co_return make_committed(store, committed, "MISS", cache_control);
    }

    // Uncacheable: the body is already in memory (identity, or raw for passthrough).
    meta.body_size = static_cast<std::int64_t>(st->body.size());
    if (st->streaming && w && !st->client_gone) {
        try {
            co_await w->finish();
        } catch (...) {
        }
        record_traffic(target.string(), "MISS-UNCACHED", resp.status, meta.body_size);
    }
    auto fr = std::make_shared<FetchResult>();
    fr->committed = false;
    fr->temp_meta = meta;
    fr->body = std::move(st->body);
    fr->cache_status = "MISS-UNCACHED";
    fr->cache_control = cache_control;
    co_return fr;
}

asio::awaitable<void> Provider::proxy_pass(server::Request& req, server::ResponseWriter& w,
                                           const Snapshot& snap, const Url& target,
                                           std::string cache_status) {
    net::ClientRequest creq;
    creq.method = req.method;
    creq.target = target;
    for (const auto& e : req.headers.entries()) {
        if (!rp_hop(e.first)) creq.headers.add(e.first, e.second);
    }
    if (method_has_body(req.method)) creq.body = req.body;

    bool is_head = req.is_head();
    // Record once the upstream headers arrive: status + Content-Length are known
    // there. Bypassed bodies stream straight through (not buffered), so the byte
    // figure reflects the advertised Content-Length when present.
    std::string turl = target.string();
    net::HeadHandler on_head = [this, &w, cs = cache_status, turl,
                                is_head](const net::ClientResponse& resp) -> asio::awaitable<void> {
        (void)is_head;
        HeaderMap h;
        for (const auto& e : resp.headers.entries()) {
            if (!rp_hop(e.first)) h.add(e.first, e.second);
        }
        h.set("X-Studio-Cache", cs);
        std::optional<std::int64_t> clen;
        if (auto cl = resp.headers.get("Content-Length"); !cl.empty()) {
            try {
                clen = std::stoll(cl);
            } catch (...) {
            }
        }
        record_traffic(turl, cs, resp.status, clen.value_or(0));
        co_await w.send_header(resp.status, std::move(h), clen);
    };

    ResponseWriterSink sink(&w, is_head);
    std::exception_ptr ferr;
    try {
        co_await snap.client->fetch(creq, on_head, sink);
    } catch (...) {
        ferr = std::current_exception();
    }
    if (ferr) {
        if (!w.header_sent()) {
            HeaderMap h;
            h.set("Content-Type", "text/plain; charset=utf-8");
            co_await w.write_full(502, std::move(h), "upstream request failed\n");
        }
        co_return;
    }
    co_await w.finish();
}

asio::awaitable<void> Provider::serve_entry(server::Request& req, server::ResponseWriter& w,
                                            std::shared_ptr<cache::Backend> store,
                                            const Metadata& meta, std::string cache_status,
                                            std::string cache_control, bool stale_warning) {
    std::string key = meta.key;
    std::string body = co_await net::run_blocking(*ctx_.blocking,
                                                  [store, key]() { return store->open_body(key); });
    record_traffic(meta.url, cache_status, meta.status_code,
                   static_cast<std::int64_t>(body.size()));

    HeaderMap h;
    for (const auto& e : meta.header.entries()) {
        if (rp_hop(e.first) || iequals(e.first, "Content-Length")) continue;
        // Drop Vary even for entries cached before it was stripped at ingest: we
        // serve one identity representation, so the response does not vary.
        if (iequals(e.first, "Vary")) continue;
        h.add(e.first, e.second);
    }
    if (!cache_control.empty()) {
        // Our override is the authoritative freshness signal. Remove the upstream
        // Expires/Pragma, which can be stale (e.g. a past Expires left by a 304
        // that refreshed Date but not Expires) and make caches behave conservatively.
        h.set("Cache-Control", cache_control);
        h.remove("Expires");
        h.remove("Pragma");
    }
    if (stale_warning) h.set("Warning", "110 - \"Response is stale\"");
    h.set("X-Studio-Cache", cache_status);
    h.set("X-Studio-Cache-Key", meta.key);
    if (!meta.version.empty()) h.set("X-Studio-Cache-Version", meta.version);
    auto age =
        std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - meta.stored_at).count();
    if (age < 0) age = 0;
    h.set("Age", std::to_string(age));
    if (!meta.body_sha256.empty()) h.set("X-Studio-Body-SHA256", meta.body_sha256);

    std::optional<TimePoint> modt = meta.stored_at;
    if (auto lm = meta.last_modified(); !lm.empty()) {
        if (auto p = parse_http_date(lm)) modt = p;
    }
    co_await server::serve_content(req, w, std::move(h), std::move(body), modt);
}

asio::awaitable<void> Provider::serve_result(server::Request& req, server::ResponseWriter& w,
                                             const FetchResult& result) {
    if (result.committed) {
        co_await serve_entry(req, w, result.store, result.meta, result.cache_status,
                             result.cache_control, false);
        co_return;
    }

    // Uncacheable result: body is held in memory (see Provider::fetch).
    record_traffic(result.temp_meta.url, result.cache_status, result.temp_meta.status_code,
                   static_cast<std::int64_t>(result.body.size()));

    HeaderMap h;
    for (const auto& e : result.temp_meta.header.entries()) {
        if (!rp_hop(e.first) && !iequals(e.first, "Content-Length")) h.add(e.first, e.second);
    }
    h.set("X-Studio-Cache", result.cache_status);

    if (req.is_head()) {
        co_await w.send_header(result.temp_meta.status_code, std::move(h),
                               result.temp_meta.body_size);
        co_await w.finish();
    } else {
        co_await w.send_header(result.temp_meta.status_code, std::move(h),
                               static_cast<std::int64_t>(result.body.size()));
        co_await w.write_chunk(result.body);
        co_await w.finish();
    }
}

asio::awaitable<host::HomepageDocument> Provider::fetch_homepage(const server::Request& req) {
    auto snap = snapshot();
    Url target = snap->upstream;
    target.clear_query_and_fragment();

    server::Request fetch_req = req;
    fetch_req.method = "GET";
    fetch_req.headers.set("Accept", "text/html,application/xhtml+xml,*/*;q=0.8");
    fetch_req.headers.remove("Content-Type");

    cache::RouteAction action = router_->match(target, snap->upstream);
    auto [store, ttl, max_bytes] = resolve_store(*snap, action);
    std::string version = cache::extract_version(action.version, target);
    std::string key = cache_key(snap->upstream, target, action, version);
    auto now = Clock::now();

    std::optional<Metadata> cached;
    try {
        cached =
            co_await net::run_blocking(*ctx_.blocking, [store, key]() { return store->get(key); });
    } catch (...) {
    }

    std::shared_ptr<FetchResult> result;
    bool version_mismatch =
        action.version_revalidate && !version.empty() && cached && cached->version != version;
    bool force_reval = cache::request_forces_revalidation(req.headers) || version_mismatch;
    if (cached && cached->fresh(now) && !force_reval) {
        maybe_touch(store, key, *cached, now);
        result = make_committed(store, *cached, "HIT", action.cache_control);
    } else {
        std::string fetch_error;
        bool failed = false;
        try {
            result = co_await flights_.do_call(
                key,
                [this, &snap, target, key, store, ttl, max_bytes, action, force_reval, version,
                 &fetch_req]() -> asio::awaitable<std::shared_ptr<FetchResult>> {
                    std::optional<Metadata> refreshed;
                    try {
                        refreshed = co_await net::run_blocking(
                            *ctx_.blocking, [store, key]() { return store->get(key); });
                    } catch (...) {
                        refreshed = std::nullopt;
                    }
                    bool reval = force_reval || (action.version_revalidate && !version.empty() &&
                                                 refreshed && refreshed->version != version);
                    if (refreshed && refreshed->fresh(Clock::now()) && !reval) {
                        co_return make_committed(store, *refreshed, "HIT", action.cache_control);
                    }
                    // Buffer mode (w == nullptr): the homepage HTML is post-processed,
                    // so collect the decoded body instead of streaming it.
                    co_return co_await fetch(
                        *snap, target, key, store, ttl, max_bytes, action.force_cache,
                        resolve_cacheable_statuses(snap->cacheable_statuses,
                                                   action.cacheable_status_codes),
                        action.cache_control, action.version_revalidate ? version : std::string(),
                        refreshed, fetch_req, nullptr);
                });
        } catch (const std::exception& e) {
            fetch_error = e.what();
            failed = true;
        }
        if (failed) {
            if (!cached || !cache::stale_if_error_servable(cached->expires_at, Clock::now(),
                                                           snap->stale_if_error_seconds)) {
                throw Error(fetch_error);
            }
            result = make_committed(store, *cached, "STALE", action.cache_control);
        }
    }

    host::HomepageDocument doc;
    doc.url = target.string();
    doc.cache_status = result->cache_status;
    if (result->committed) {
        std::string skey = result->meta.key;
        auto s = result->store;
        doc.status_code = result->meta.status_code;
        doc.header = result->meta.header;
        doc.body =
            co_await net::run_blocking(*ctx_.blocking, [s, skey]() { return s->open_body(skey); });
    } else {
        doc.status_code = result->temp_meta.status_code;
        doc.header = result->temp_meta.header;
        doc.body = result->body;
    }
    record_traffic(doc.url, doc.cache_status, doc.status_code,
                   static_cast<std::int64_t>(doc.body.size()));
    co_return doc;
}

}  // namespace sbc::host::reverseproxy
