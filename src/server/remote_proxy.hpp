#pragma once

#include <optional>
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

// remote_loader_target detects a /http(s)://... request and returns the embedded
// absolute URL (inheriting the request query when the embedded URL has none).
std::optional<Url> remote_loader_target(const Request& req);

// serve_game_socket forwards /socket.io/ to the configured game server with
// spoofed Origin/Referer, dispatching to a WebSocket relay or a direct HTTP
// proxy (long-poll) based on the request.
boost::asio::awaitable<void> serve_game_socket(Request& req, ResponseWriter& w,
                                               const std::string& game_server,
                                               const std::string& upstream, net::TlsContext& tls,
                                               boost::asio::any_io_executor ex);

// serve_direct_remote proxies a request to `target` without caching, forcing
// Cache-Control: no-store and X-Studio-Remote-Proxy: DIRECT.
boost::asio::awaitable<void> serve_direct_remote(Request& req, ResponseWriter& w, const Url& target,
                                                 net::TlsContext& tls,
                                                 boost::asio::any_io_executor ex);

}  // namespace sbc::server
