#pragma once

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>

namespace sbc::net {

// TcpStream is the server-side connection type. The local admin/proxy server
// listens on plain TCP (127.0.0.1); TLS is only used on the upstream/client
// side, so server connections never need an SSL layer.
using TcpStream = boost::beast::tcp_stream;

// HijackedConnection is handed to handlers that take over the raw socket
// (WebSocket upgrade, SSE). It carries the moved stream plus any bytes the HTTP
// parser already buffered.
struct HijackedConnection {
    TcpStream stream;
    boost::beast::flat_buffer buffer;
};

}  // namespace sbc::net
