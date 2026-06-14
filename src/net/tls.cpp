#include "net/tls.hpp"

namespace sbc::net {

namespace ssl = boost::asio::ssl;

TlsContext::TlsContext() : ctx_(ssl::context::tls_client) {
    ctx_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                     ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_peer);
}

}  // namespace sbc::net
