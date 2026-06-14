#include "server/remote_proxy.hpp"

#include <array>
#include <cctype>

#include "common/error.hpp"
#include "net/http_client.hpp"
#include "net/tls.hpp"
#include "server/api_util.hpp"
#include "server/websocket_proxy.hpp"

namespace sbc::server {

namespace asio = boost::asio;

namespace {

bool rp_hop(std::string_view key) {
    static const std::array<const char*, 8> hop = {"connection", "keep-alive",
                                                   "proxy-authenticate", "proxy-authorization",
                                                   "te",         "trailer",
                                                   "transfer-encoding", "upgrade"};
    for (const char* h : hop) {
        if (iequals(key, h)) return true;
    }
    return false;
}

bool method_has_body(const std::string& method) {
    return method != "GET" && method != "HEAD" && method != "OPTIONS";
}

class ClientStreamSink : public net::BodySink {
public:
    ClientStreamSink(ResponseWriter* w, bool skip) : w_(w), skip_(skip) {}
    asio::awaitable<void> on_chunk(std::string_view data) override {
        if (!skip_) co_await w_->write_chunk(data);
    }

private:
    ResponseWriter* w_;
    bool skip_;
};

}  // namespace

std::optional<Url> remote_loader_target(const Request& req) {
    // Use the percent-encoded path, not the decoded req.path. The embedded URL is
    // re-parsed as a URL below, so its percent-encoding must be preserved: a decoded
    // path turns escapes like %E9%A9%AC (UTF-8 "马") into raw non-ASCII bytes, which
    // are not valid URI characters and make parse_uri_reference fail -> spurious 404.
    std::string raw;
    if (auto u = Url::try_parse(req.target)) {
        raw = u->encoded_path();
    } else {
        raw = req.path;
    }
    if (!raw.empty() && raw.front() == '/') raw.erase(raw.begin());
    if (raw.rfind("http://", 0) != 0 && raw.rfind("https://", 0) != 0) return std::nullopt;

    auto target = Url::try_parse(raw);
    if (!target || target->scheme().empty() || target->host().empty()) return std::nullopt;
    // The embedded URL's query (if any) arrives as the request query; inherit it.
    if (!target->has_query() && !req.raw_query.empty()) target->set_query(req.raw_query);
    return target;
}

asio::awaitable<void> serve_direct_remote(Request& req, ResponseWriter& w, const Url& target,
                                          net::TlsContext& tls, asio::any_io_executor ex) {
    net::HttpClient client(ex, tls, std::nullopt);

    net::ClientRequest creq;
    creq.method = req.method;
    creq.target = target;
    for (const auto& e : req.headers.entries()) {
        if (!rp_hop(e.first)) creq.headers.add(e.first, e.second);
    }
    if (method_has_body(req.method)) creq.body = req.body;

    bool is_head = req.is_head();
    net::HeadHandler on_head =
        [&w, is_head](const net::ClientResponse& resp) -> asio::awaitable<void> {
        (void)is_head;
        HeaderMap h;
        for (const auto& e : resp.headers.entries()) {
            if (!rp_hop(e.first)) h.add(e.first, e.second);
        }
        h.set("Cache-Control", "no-store");
        h.set("X-Studio-Remote-Proxy", "DIRECT");
        std::optional<std::int64_t> clen;
        if (auto cl = resp.headers.get("Content-Length"); !cl.empty()) {
            try {
                clen = std::stoll(cl);
            } catch (...) {
            }
        }
        co_await w.send_header(resp.status, std::move(h), clen);
    };

    ClientStreamSink sink(&w, is_head);
    std::exception_ptr ferr;
    try {
        co_await client.fetch(creq, on_head, sink);
    } catch (...) {
        ferr = std::current_exception();
    }
    if (ferr) {
        if (!w.header_sent()) co_await write_error(w, 502, "upstream request failed");
        co_return;
    }
    co_await w.finish();
}

asio::awaitable<void> serve_game_socket(Request& req, ResponseWriter& w,
                                        const std::string& game_server, const std::string& upstream,
                                        net::TlsContext& tls, asio::any_io_executor ex) {
    auto base = Url::try_parse(game_server);
    if (!base || base->host().empty()) {
        co_await write_error(w, 500, "invalid gameServer configuration");
        co_return;
    }

    Url target = *base;
    std::string req_path = "/";
    if (auto u = Url::try_parse(req.target)) req_path = u->encoded_path();
    target.set_path(req_path);
    target.set_query(req.raw_query);

    // Spoof Origin/Referer to look like a page served from the upstream mirror.
    std::string spoofed_origin;
    if (auto up = Url::try_parse(upstream); up && !up->host().empty()) {
        spoofed_origin = up->scheme() + "://" + up->authority();
        Url referer = *up;
        referer.ensure_trailing_slash();
        referer.clear_query_and_fragment();
        req.headers.set("Origin", spoofed_origin);
        req.headers.set("Referer", referer.string());
    }

    bool is_upgrade = iequals(req.headers.get("Upgrade"), "websocket") &&
                      header_contains_token(req.headers.get("Connection"), "upgrade");
    if (is_upgrade) {
        co_await relay_websocket(req, w, target, spoofed_origin, tls, ex);
        co_return;
    }
    co_await serve_direct_remote(req, w, target, tls, ex);
}

}  // namespace sbc::server
