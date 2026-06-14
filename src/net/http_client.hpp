#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "common/http_util.hpp"
#include "common/url.hpp"
#include "net/socks5.hpp"

namespace sbc::net {

class TlsContext;

struct ClientResponse {
    int status = 0;
    HeaderMap headers;
};

struct ClientRequest {
    std::string method = "GET";
    Url target;
    HeaderMap headers;
    std::string body;  // request body (empty for GET/HEAD)
};

// BodySink receives streamed response-body chunks. on_chunk is a coroutine so a
// sink can perform async work (e.g. stream to the client connection).
class BodySink {
public:
    virtual ~BodySink() = default;
    virtual boost::asio::awaitable<void> on_chunk(std::string_view data) = 0;
};

// StringSink collects the whole body into memory.
class StringSink : public BodySink {
public:
    boost::asio::awaitable<void> on_chunk(std::string_view data) override {
        body.append(data);
        co_return;
    }
    std::string body;
};

// HeadHandler, if set, runs after the response header is parsed and before the
// body streams — used by proxy-pass to send the client header first.
using HeadHandler = std::function<boost::asio::awaitable<void>(const ClientResponse&)>;

// HttpClient performs upstream HTTP/1.1 requests (TCP or TLS, optionally via
// SOCKS5), streaming the response body to a sink. Idle connections are kept
// alive and reused per (scheme, host, port) so repeated fetches to the same
// upstream avoid a fresh TCP+TLS handshake each time. A pooled connection that
// turns out to be stale (closed by the peer while idle) is transparently
// retried once on a new connection, provided nothing has been streamed yet.
class HttpClient {
public:
    HttpClient(boost::asio::any_io_executor executor, TlsContext& tls,
               std::optional<Socks5Config> socks5,
               std::chrono::seconds timeout = std::chrono::seconds(120));

    boost::asio::awaitable<ClientResponse> fetch(const ClientRequest& req,
                                                 const HeadHandler& on_head, BodySink& sink);

private:
    boost::asio::awaitable<boost::asio::ip::tcp::socket> dial(const std::string& host,
                                                              std::uint16_t port);
    boost::asio::awaitable<ClientResponse> fetch_plain(const std::string& host, std::uint16_t port,
                                                       const ClientRequest& req,
                                                       const HeadHandler& on_head, BodySink& sink);
    boost::asio::awaitable<ClientResponse> fetch_tls(const std::string& host, std::uint16_t port,
                                                     const ClientRequest& req,
                                                     const HeadHandler& on_head, BodySink& sink);

    struct Pool;  // idle-connection pool, defined in the .cpp

    boost::asio::any_io_executor executor_;
    TlsContext& tls_;
    std::optional<Socks5Config> socks5_;
    std::chrono::seconds timeout_;
    std::shared_ptr<Pool> pool_;
};

}  // namespace sbc::net
