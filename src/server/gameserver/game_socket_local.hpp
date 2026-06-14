#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server::gameserver::socketio {
class SocketIoServer;
}

namespace sbc::server {

// serve_game_socket_local handles "/socket.io/*" against the embedded local game
// server hub, the local-mode counterpart to serve_game_socket (the remote proxy).
boost::asio::awaitable<void> serve_game_socket_local(Request& req, ResponseWriter& w,
                                                     gameserver::socketio::SocketIoServer& hub,
                                                     boost::asio::any_io_executor ex);

}  // namespace sbc::server
