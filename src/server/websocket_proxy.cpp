#include "server/websocket_proxy.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "net/tls.hpp"

// Backend-specific Beast ↔ TLS-stream glue: Beast's websocket close path needs an
// async_teardown overload for the secure next layer. wintls ships these in its
// separate <wintls/beast.hpp> (not pulled in by <wintls.hpp>); the OpenSSL backend
// uses Beast's own ssl teardown helpers.
#ifdef _WIN32
#include <wintls/beast.hpp>
#else
#include <boost/beast/websocket/ssl.hpp>
#endif

namespace sbc::server {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;
using namespace asio::experimental::awaitable_operators;

namespace {

// pump relays messages from `from` to `to` until an error/close. Returns
// (never throws) so a parallel-group sees clean completion.
template <typename From, typename To>
asio::awaitable<void> pump(From& from, To& to) {
    beast::flat_buffer buffer;
    for (;;) {
        auto [rec, rn] = co_await from.async_read(buffer, asio::as_tuple(asio::use_awaitable));
        (void)rn;
        if (rec) co_return;
        to.binary(from.got_binary());
        auto [wec, wn] =
            co_await to.async_write(buffer.data(), asio::as_tuple(asio::use_awaitable));
        (void)wn;
        buffer.consume(buffer.size());
        if (wec) co_return;
    }
}

template <typename ClientWs, typename UpstreamWs>
asio::awaitable<void> relay_pumps(ClientWs& client, UpstreamWs& upstream) {
    co_await (pump(client, upstream) || pump(upstream, client));
    boost::system::error_code ec;
    co_await client.async_close(websocket::close_code::normal,
                                asio::redirect_error(asio::use_awaitable, ec));
    co_await upstream.async_close(websocket::close_code::normal,
                                  asio::redirect_error(asio::use_awaitable, ec));
}

http::request<http::empty_body> rebuild_request(const Request& req) {
    http::request<http::empty_body> out;
    out.method(http::verb::get);
    out.target(req.target);
    out.version(11);
    for (const auto& e : req.headers.entries()) out.insert(e.first, e.second);
    return out;
}

}  // namespace

asio::awaitable<void> relay_websocket(Request& req, ResponseWriter& w, const Url& target,
                                      const std::string& spoofed_origin, net::TlsContext& tls,
                                      asio::any_io_executor ex) {
    net::HijackedConnection conn = w.hijack();
    websocket::stream<beast::tcp_stream> client(std::move(conn.stream));
    client.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    co_await client.async_accept(rebuild_request(req), asio::use_awaitable);

    std::string host = target.host();
    std::uint16_t port = target.port();
    std::string path = target.encoded_path();
    if (target.has_query()) path += "?" + target.query();
    if (path.empty()) path = "/";

    std::string subprotocol = req.headers.get("Sec-WebSocket-Protocol");
    std::string user_agent = req.headers.get("User-Agent");
    auto decorator = [spoofed_origin, subprotocol, user_agent](websocket::request_type& m) {
        if (!spoofed_origin.empty()) m.set(http::field::origin, spoofed_origin);
        if (!subprotocol.empty()) m.set(http::field::sec_websocket_protocol, subprotocol);
        if (!user_agent.empty()) m.set(http::field::user_agent, user_agent);
    };

    tcp::resolver resolver(ex);
    auto endpoints = co_await resolver.async_resolve(host, std::to_string(port),
                                                     asio::use_awaitable);
    tcp::socket socket(ex);
    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);

    // Relay both directions on a single strand. The client/upstream streams live
    // on the bare (multi-threaded) io_context, so without this the two concurrent
    // pumps hop across threads on the *same* stream — and a read that auto-replies
    // to a ping/close frame races the opposite pump's write. co_spawning the relay
    // on a strand makes use_awaitable's associated executor the strand, so Beast
    // serializes every read/write (and its internal control-frame writes) there.
    auto strand = asio::make_strand(ex);

    const bool secure = target.is_https() || target.scheme() == "wss";
    if (secure) {
        websocket::stream<net::TlsStream> upstream(std::move(socket), tls.context());
        net::tls_set_client_hostname(upstream.next_layer(), host);
        co_await upstream.next_layer().async_handshake(net::kTlsHandshakeClient,
                                                       asio::use_awaitable);
        upstream.set_option(websocket::stream_base::decorator(decorator));
        co_await upstream.async_handshake(host, path, asio::use_awaitable);
        co_await asio::co_spawn(strand, relay_pumps(client, upstream), asio::use_awaitable);
    } else {
        websocket::stream<beast::tcp_stream> upstream(std::move(socket));
        upstream.set_option(websocket::stream_base::decorator(decorator));
        co_await upstream.async_handshake(host, path, asio::use_awaitable);
        co_await asio::co_spawn(strand, relay_pumps(client, upstream), asio::use_awaitable);
    }
}

}  // namespace sbc::server
