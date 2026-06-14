#pragma once

#include <string>

#include <boost/beast/core/tcp_stream.hpp>

#ifdef _WIN32
#include <boost/wintls.hpp>
#else
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#endif

namespace sbc::net {

// The TLS layer is abstracted so the upstream HTTP/WSS code is backend-agnostic.
// On Windows it is Schannel (via the header-only wintls wrapper) so no OpenSSL is
// linked or shipped; everywhere else it is OpenSSL through Boost.Asio.SSL. Both
// backends expose an asio-compatible stream, so the call sites differ only in how
// SNI/verification is applied (tls_set_client_hostname) and the handshake-type
// constant below.
#ifdef _WIN32
using NativeTlsContext = boost::wintls::context;
using TlsStream = boost::wintls::stream<boost::beast::tcp_stream>;
inline constexpr auto kTlsHandshakeClient = boost::wintls::handshake_type::client;
#else
using NativeTlsContext = boost::asio::ssl::context;
using TlsStream = boost::asio::ssl::stream<boost::beast::tcp_stream>;
inline constexpr auto kTlsHandshakeClient = boost::asio::ssl::stream_base::client;
#endif

// TlsContext owns the shared client-side TLS context (TLS 1.2+), used for all
// HTTPS upstream and WSS game-socket connections. System CA roots are used for
// peer verification; SNI and hostname verification are applied per-connection via
// tls_set_client_hostname.
class TlsContext {
public:
    TlsContext();

    NativeTlsContext& context() { return ctx_; }

private:
    NativeTlsContext ctx_;
};

// Applies the SNI host name plus peer/hostname verification (and, on the OpenSSL
// backend, the http/1.1 ALPN hint) to a freshly created client stream, before its
// handshake. Throws on failure to set the SNI host name.
void tls_set_client_hostname(TlsStream& stream, const std::string& host);

}  // namespace sbc::net