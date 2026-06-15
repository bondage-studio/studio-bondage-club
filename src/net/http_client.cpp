#include "net/http_client.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <exception>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "net/tls.hpp"

namespace sbc::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;
using SteadyClock = std::chrono::steady_clock;

namespace {

constexpr auto kIdleMax = std::chrono::seconds(90);
constexpr std::size_t kMaxIdlePerHost = 8;

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

using PlainStream = beast::tcp_stream;

template <class Stream>
struct PoolEntry {
    std::unique_ptr<Stream> stream;
    SteadyClock::time_point idle_since;
};

// Take an idle connection for `key`, pruning ones idle past kIdleMax. Returns
// null if none usable.
template <class Stream>
std::unique_ptr<Stream> pool_take(std::mutex& mu,
                                  std::unordered_multimap<std::string, PoolEntry<Stream>>& map,
                                  const std::string& key) {
    std::lock_guard<std::mutex> lk(mu);
    auto now = SteadyClock::now();
    auto it = map.find(key);
    while (it != map.end() && it->first == key) {
        if (now - it->second.idle_since > kIdleMax) {
            it = map.erase(it);
            continue;
        }
        auto s = std::move(it->second.stream);
        map.erase(it);
        return s;
    }
    return nullptr;
}

// Return a still-usable connection to the pool, or drop it if at capacity.
template <class Stream>
void pool_put(std::mutex& mu, std::unordered_multimap<std::string, PoolEntry<Stream>>& map,
              const std::string& key, std::unique_ptr<Stream> stream) {
    std::lock_guard<std::mutex> lk(mu);
    if (map.count(key) >= kMaxIdlePerHost) return;  // unique_ptr drops -> socket closes
    map.insert({key, PoolEntry<Stream>{std::move(stream), SteadyClock::now()}});
}

void tcp_shutdown(beast::tcp_stream& s) {
    boost::system::error_code ec;
    s.socket().shutdown(tcp::socket::shutdown_both, ec);
}

// Write the request, parse the response header, then stream the body to `sink`.
// `committed` is set once the response header has been read — past that point a
// failure must not be retried on a new connection (downstream delivery began).
// `reusable` reports whether the connection may be returned to the idle pool.
template <class Stream>
asio::awaitable<ClientResponse> do_request(Stream& stream, const ClientRequest& req,
                                           const HeadHandler& on_head, BodySink& sink,
                                           bool& committed, bool& reusable) {
    http::request<http::string_body> hreq;
    hreq.version(11);
    hreq.method_string(req.method);

    std::string target = req.target.encoded_path();
    if (target.empty()) target = "/";
    if (req.target.has_query()) target += "?" + req.target.query();
    hreq.target(target);

    std::string host = req.target.host();
    std::string port = req.target.port_string();
    hreq.set(http::field::host, port.empty() ? host : host + ":" + port);

    for (const auto& e : req.headers.entries()) {
        if (iequals(e.first, "Host")) continue;
        hreq.insert(e.first, e.second);
    }
    if (!req.body.empty()) hreq.body() = req.body;
    hreq.prepare_payload();

    co_await http::async_write(stream, hreq, asio::use_awaitable);

    beast::flat_buffer buffer;
    http::response_parser<http::buffer_body> parser;
    parser.body_limit(boost::none);
    parser.skip(iequals(req.method, "HEAD"));
    co_await http::async_read_header(stream, buffer, parser, asio::use_awaitable);
    committed = true;

    ClientResponse resp;
    resp.status = parser.get().result_int();
    for (const auto& f : parser.get().base()) {
        resp.headers.add(std::string(f.name_string()), std::string(f.value()));
    }

    if (on_head) co_await on_head(resp);

    std::vector<char> buf(64 * 1024);
    while (!parser.is_done()) {
        parser.get().body().data = buf.data();
        parser.get().body().size = buf.size();
        auto [ec, n] =
            co_await http::async_read(stream, buffer, parser, asio::as_tuple(asio::use_awaitable));
        (void)n;
        std::size_t got = buf.size() - parser.get().body().size;
        if (got > 0) co_await sink.on_chunk(std::string_view(buf.data(), got));
        if (ec == http::error::need_buffer) continue;
        if (ec) throw boost::system::system_error(ec);
    }

    reusable = parser.is_done() && parser.get().keep_alive();
    co_return resp;
}

}  // namespace

struct HttpClient::Pool {
    std::mutex mu;
    std::unordered_multimap<std::string, PoolEntry<PlainStream>> plain;
    std::unordered_multimap<std::string, PoolEntry<TlsStream>> tls;
};

HttpClient::HttpClient(asio::any_io_executor executor, TlsContext& tls,
                       std::optional<Socks5Config> socks5, std::chrono::seconds timeout)
    : executor_(std::move(executor)),
      tls_(tls),
      socks5_(std::move(socks5)),
      timeout_(timeout),
      pool_(std::make_shared<Pool>()) {}

asio::awaitable<tcp::socket> HttpClient::dial(const std::string& host, std::uint16_t port) {
    if (socks5_) {
        co_return co_await socks5_connect(executor_, *socks5_, host, port);
    }
    tcp::resolver resolver(executor_);
    auto endpoints =
        co_await resolver.async_resolve(host, std::to_string(port), asio::use_awaitable);
    tcp::socket socket(executor_);
    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
    co_return socket;
}

asio::awaitable<ClientResponse> HttpClient::fetch_plain(const std::string& host, std::uint16_t port,
                                                        const ClientRequest& req,
                                                        const HeadHandler& on_head,
                                                        BodySink& sink) {
    std::string key = host + ":" + std::to_string(port);
    for (int attempt = 0;; ++attempt) {
        // Only the first attempt reuses a pooled connection; a retry always dials fresh.
        std::unique_ptr<PlainStream> conn =
            attempt == 0 ? pool_take(pool_->mu, pool_->plain, key) : nullptr;
        bool reused = static_cast<bool>(conn);
        if (!conn) {
            tcp::socket socket = co_await dial(host, port);
            conn = std::make_unique<PlainStream>(std::move(socket));
        }
        conn->expires_after(timeout_);

        bool committed = false, reusable = false;
        std::optional<ClientResponse> resp;
        std::exception_ptr eptr;
        try {
            resp = co_await do_request(*conn, req, on_head, sink, committed, reusable);
        } catch (...) {
            eptr = std::current_exception();
        }
        if (eptr) {
            tcp_shutdown(*conn);
            conn.reset();
            if (reused && !committed) continue;  // stale idle connection -> retry once, fresh
            std::rethrow_exception(eptr);
        }
        if (reusable) {
            conn->expires_never();
            pool_put(pool_->mu, pool_->plain, key, std::move(conn));
        } else {
            tcp_shutdown(*conn);
        }
        co_return std::move(*resp);
    }
}

asio::awaitable<ClientResponse> HttpClient::fetch_tls(const std::string& host, std::uint16_t port,
                                                      const ClientRequest& req,
                                                      const HeadHandler& on_head, BodySink& sink) {
    std::string key = host + ":" + std::to_string(port);
    for (int attempt = 0;; ++attempt) {
        std::unique_ptr<TlsStream> conn =
            attempt == 0 ? pool_take(pool_->mu, pool_->tls, key) : nullptr;
        bool reused = static_cast<bool>(conn);
        if (!conn) {
            tcp::socket socket = co_await dial(host, port);
            beast::tcp_stream tcp_layer(std::move(socket));
            tcp_layer.expires_after(timeout_);
            conn = std::make_unique<TlsStream>(std::move(tcp_layer), tls_.context());
            tls_set_client_hostname(*conn, host);
            co_await conn->async_handshake(kTlsHandshakeClient, asio::use_awaitable);
        }
        beast::get_lowest_layer(*conn).expires_after(timeout_);

        bool committed = false, reusable = false;
        std::optional<ClientResponse> resp;
        std::exception_ptr eptr;
        try {
            resp = co_await do_request(*conn, req, on_head, sink, committed, reusable);
        } catch (...) {
            eptr = std::current_exception();
        }
        if (eptr) {
            tcp_shutdown(beast::get_lowest_layer(*conn));
            conn.reset();
            if (reused && !committed) continue;  // stale idle connection -> retry once, fresh
            std::rethrow_exception(eptr);
        }
        if (reusable) {
            beast::get_lowest_layer(*conn).expires_never();
            pool_put(pool_->mu, pool_->tls, key, std::move(conn));
        } else {
            tcp_shutdown(beast::get_lowest_layer(*conn));
        }
        co_return std::move(*resp);
    }
}

asio::awaitable<ClientResponse> HttpClient::fetch(const ClientRequest& req,
                                                  const HeadHandler& on_head, BodySink& sink) {
    std::string host = req.target.host();
    std::uint16_t port = req.target.port();
    if (req.target.is_https()) {
        co_return co_await fetch_tls(host, port, req, on_head, sink);
    }
    co_return co_await fetch_plain(host, port, req, on_head, sink);
}

}  // namespace sbc::net
