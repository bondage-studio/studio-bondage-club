#include "net/tls.hpp"

#ifndef _WIN32
#include <openssl/ssl.h>

#include <boost/asio/ssl/host_name_verification.hpp>

#include "common/error.hpp"
#endif

namespace sbc::net {

#ifdef _WIN32

// Windows: Schannel via wintls. The system trust store provides the CA roots and
// performs chain + hostname validation during the handshake.
TlsContext::TlsContext() : ctx_(wintls::method::system_default) {
    ctx_.use_default_certificates(true);
    ctx_.verify_server_certificate(true);
}

void tls_set_client_hostname(TlsStream& stream, const std::string& host) {
    // Sets the SNI host name and the name checked against the certificate during
    // the handshake (chain validation against the Windows trust store).
    stream.set_server_hostname(host);
}

#else

namespace ssl = boost::asio::ssl;

TlsContext::TlsContext() : ctx_(ssl::context::tls_client) {
    ctx_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                     ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_peer);
}

void tls_set_client_hostname(TlsStream& stream, const std::string& host) {
    if (SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
        throw Error("tls: failed to set SNI host name");
    }
    stream.set_verify_callback(ssl::host_name_verification(host));
    static const unsigned char kAlpn[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    SSL_set_alpn_protos(stream.native_handle(), kAlpn, sizeof(kAlpn));
}

#endif

}  // namespace sbc::net