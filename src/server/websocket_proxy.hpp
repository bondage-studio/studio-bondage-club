#pragma once

#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include "common/url.hpp"
#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::net {
class TlsContext;
}

namespace sbc::server {

// relay_websocket hijacks the client connection, opens a WebSocket to `target`
// (ws/wss) injecting the spoofed Origin, and relays frames bidirectionally until
// either side closes. Used for the socket.io WebSocket transport.
boost::asio::awaitable<void> relay_websocket(Request& req, ResponseWriter& w, const Url& target,
                                             const std::string& spoofed_origin,
                                             net::TlsContext& tls, boost::asio::any_io_executor ex);

}  // namespace sbc::server
