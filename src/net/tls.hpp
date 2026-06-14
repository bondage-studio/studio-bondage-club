#pragma once

#include <boost/asio/ssl/context.hpp>

namespace sbc::net {

// TlsContext owns the shared client-side SSL context (TLS 1.2+), used for all
// HTTPS upstream and WSS game-socket connections. System CA roots are loaded for
// peer verification; SNI and hostname verification are applied per-connection.
class TlsContext {
public:
    TlsContext();

    boost::asio::ssl::context& context() { return ctx_; }

private:
    boost::asio::ssl::context ctx_;
};

}  // namespace sbc::net
