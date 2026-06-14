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
#include "common/error.hpp"
#include "net/blocking_pool.hpp"
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
    std::shared_ptr<cache::Backend> store; // for committed entries
    Metadata meta; // committed metadata
    fs::path temp_path; // uncacheable temp body
    std::shared_ptr<cache::TempFileGuard> cleanup;
    Metadata temp_meta;
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

// Hop-by-hop set used by the reverse proxy (matches Go reverseproxy: includes
// Upgrade, excludes Proxy-Connection).
bool rp_hop(std::string_view key) {
    static const std::array hop = {
        "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
        "te", "trailer", "transfer-encoding", "upgrade"
    };
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

std::string read_whole_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// WriterSink streams the upstream body into a cache temp file (sync writes).
class WriterSink : public net::BodySink {
public:
    explicit WriterSink(cache::Writer* writer) : writer_(writer) {}
    asio::awaitable<void> on_chunk(std::string_view data) override {
        writer_->write(data);
        co_return;
    }

private:
    cache::Writer* writer_;
};

// ResponseWriterSink streams the upstream body straight to the client.
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
    p->snap_ = p->build_snapshot(cfg);  // throws on invalid upstream
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
        {"stale-on-error", "Stale fallback", true,
         "Serves an existing cached file when the upstream request fails."},
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
                                const cache::RouteAction& action) const {
    if (action.key_mode == "path") {
        std::string tpath = target.path();
        std::string base = upstream.path();
        std::string real = tpath;
        if (!base.empty() && tpath.rfind(base, 0) == 0) real = tpath.substr(base.size());
        if (real.empty() || real.front() != '/') real = "/" + real;
        return cache::key_from_path(real);
    }
    return cache::key(target.string());
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
    std::string key = cache_key(snap.upstream, target, action);
    auto now = Clock::now();

    std::optional<Metadata> cached;
    try {
        cached = co_await net::run_blocking(*ctx_.blocking,
                                            [store, key]() { return store->get(key); });
    } catch (const std::exception& e) {
        spdlog::warn("ignore invalid cache entry key={} error={}", key, e.what());
    }

    bool force_reval = cache::request_forces_revalidation(req.headers);
    if (cached && cached->fresh(now) && !force_reval) {
        schedule_touch(store, key, now);
        co_await serve_entry(req, w, store, *cached, "HIT", action.cache_control, false);
        co_return;
    }

    if (req.is_head()) {
        if (cached && !force_reval) {
            schedule_touch(store, key, now);
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
            key, [this, &snap, target, key, store, ttl, max_bytes, action, force_reval,
                  &req]() -> asio::awaitable<std::shared_ptr<FetchResult>> {
                std::optional<Metadata> refreshed;
                try {
                    refreshed = co_await net::run_blocking(
                        *ctx_.blocking, [store, key]() { return store->get(key); });
                } catch (...) {
                    refreshed = std::nullopt;
                }
                if (refreshed && refreshed->fresh(Clock::now()) && !force_reval) {
                    co_return make_committed(store, *refreshed, "HIT", action.cache_control);
                }
                co_return co_await fetch(snap, target, key, store, ttl, max_bytes,
                                         action.force_cache, action.cache_control, refreshed, req);
            });
    } catch (const std::exception& e) {
        fetch_error = e.what();
        failed = true;
    }

    if (failed) {
        if (cached) {
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

    co_await serve_result(req, w, *result);
}

asio::awaitable<std::shared_ptr<FetchResult>> Provider::fetch(
    const Snapshot& snap, Url target, std::string key, std::shared_ptr<cache::Backend> store,
    std::chrono::seconds ttl, std::int64_t max_bytes, bool force_cache, std::string cache_control,
    std::optional<Metadata> cached, server::Request& req) {
    net::ClientRequest creq;
    creq.method = "GET";
    creq.target = target;
    for (const auto& e : req.headers.entries()) {
        if (!rp_hop(e.first) && !iequals(e.first, "Range")) creq.headers.add(e.first, e.second);
    }
    creq.headers.set("Accept-Encoding", "identity");
    if (cached) {
        if (auto et = cached->etag(); !et.empty()) creq.headers.set("If-None-Match", et);
        if (auto lm = cached->last_modified(); !lm.empty())
            creq.headers.set("If-Modified-Since", lm);
    }

    auto writer = co_await net::run_blocking(*ctx_.blocking,
                                             [store, key]() { return store->new_writer(key); });
    cache::Writer* wptr = writer.get();
    WriterSink sink(wptr);

    net::ClientResponse resp;
    std::exception_ptr ferr;
    try {
        resp = co_await snap.client->fetch(creq, nullptr, sink);
    } catch (...) {
        ferr = std::current_exception();
    }
    if (ferr) {
        co_await net::run_blocking(*ctx_.blocking, [wptr]() { wptr->abort(); });
        std::rethrow_exception(ferr);
    }

    auto now = Clock::now();

    if (resp.status == 304 && cached) {
        HeaderMap merged = merge_headers(cached->header, sanitize(resp.headers));
        auto updated = co_await net::run_blocking(
            *ctx_.blocking, [store, key, merged, now, ttl]() {
                return store->update_metadata(key, [&](Metadata m) {
                    m.header = merged;
                    m.stored_at = now;
                    m.expires_at = cache::expiration(now, ttl);
                    return m;
                });
            });
        co_await net::run_blocking(*ctx_.blocking, [wptr]() { wptr->abort(); });
        Metadata meta = updated ? *updated : *cached;
        co_return make_committed(store, meta, "REVALIDATED", cache_control);
    }

    if (cached && resp.status >= 500) {
        co_await net::run_blocking(*ctx_.blocking, [wptr]() { wptr->abort(); });
        throw Error("upstream returned status " + std::to_string(resp.status));
    }

    Metadata meta;
    meta.key = key;
    meta.url = target.string();
    meta.status_code = resp.status;
    meta.header = sanitize(resp.headers);
    meta.stored_at = now;
    meta.expires_at = cache::expiration(now, ttl);

    bool cacheable = cache::response_cacheable("GET", creq.headers, resp.status, resp.headers) ||
                     (force_cache && resp.status == 200);
    if (cacheable) {
        Metadata committed = co_await net::run_blocking(
            *ctx_.blocking, [wptr, meta]() { return wptr->commit(meta); });
        co_await net::run_blocking(*ctx_.blocking,
                                   [store, max_bytes]() { store->enforce_max_size(max_bytes); });
        co_return make_committed(store, committed, "MISS", cache_control);
    }

    auto kept = co_await net::run_blocking(*ctx_.blocking, [wptr]() { return wptr->keep_temp(); });
    std::error_code ec;
    auto sz = fs::file_size(kept.first, ec);
    meta.body_size = ec ? 0 : static_cast<std::int64_t>(sz);

    auto fr = std::make_shared<FetchResult>();
    fr->committed = false;
    fr->temp_path = kept.first;
    fr->cleanup = kept.second;
    fr->temp_meta = meta;
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
    net::HeadHandler on_head = [&w, cs = cache_status, is_head](
                                   const net::ClientResponse& resp) -> asio::awaitable<void> {
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
    std::string body =
        co_await net::run_blocking(*ctx_.blocking, [store, key]() { return store->open_body(key); });

    HeaderMap h;
    for (const auto& e : meta.header.entries()) {
        if (!rp_hop(e.first) && !iequals(e.first, "Content-Length")) h.add(e.first, e.second);
    }
    if (!cache_control.empty()) h.set("Cache-Control", cache_control);
    if (stale_warning) h.set("Warning", "110 - \"Response is stale\"");
    h.set("X-Studio-Cache", cache_status);
    h.set("X-Studio-Cache-Key", meta.key);
    auto age = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - meta.stored_at)
                   .count();
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

    fs::path path = result.temp_path;
    std::string body =
        co_await net::run_blocking(*ctx_.blocking, [path]() { return read_whole_file(path); });

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
                               static_cast<std::int64_t>(body.size()));
        co_await w.write_chunk(body);
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
    std::string key = cache_key(snap->upstream, target, action);
    auto now = Clock::now();

    std::optional<Metadata> cached;
    try {
        cached = co_await net::run_blocking(*ctx_.blocking,
                                            [store, key]() { return store->get(key); });
    } catch (...) {
    }

    std::shared_ptr<FetchResult> result;
    bool force_reval = cache::request_forces_revalidation(req.headers);
    if (cached && cached->fresh(now) && !force_reval) {
        schedule_touch(store, key, now);
        result = make_committed(store, *cached, "HIT", action.cache_control);
    } else {
        std::string fetch_error;
        bool failed = false;
        try {
            result = co_await flights_.do_call(
                key, [this, &snap, target, key, store, ttl, max_bytes, action, force_reval,
                      &fetch_req]() -> asio::awaitable<std::shared_ptr<FetchResult>> {
                    std::optional<Metadata> refreshed;
                    try {
                        refreshed = co_await net::run_blocking(
                            *ctx_.blocking, [store, key]() { return store->get(key); });
                    } catch (...) {
                        refreshed = std::nullopt;
                    }
                    if (refreshed && refreshed->fresh(Clock::now()) && !force_reval) {
                        co_return make_committed(store, *refreshed, "HIT", action.cache_control);
                    }
                    co_return co_await fetch(*snap, target, key, store, ttl, max_bytes,
                                             action.force_cache, action.cache_control, refreshed,
                                             fetch_req);
                });
        } catch (const std::exception& e) {
            fetch_error = e.what();
            failed = true;
        }
        if (failed) {
            if (!cached) throw Error(fetch_error);
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
        doc.body = co_await net::run_blocking(*ctx_.blocking,
                                              [s, skey]() { return s->open_body(skey); });
    } else {
        fs::path path = result->temp_path;
        doc.status_code = result->temp_meta.status_code;
        doc.header = result->temp_meta.header;
        doc.body =
            co_await net::run_blocking(*ctx_.blocking, [path]() { return read_whole_file(path); });
    }
    co_return doc;
}

}  // namespace sbc::host::reverseproxy
