#include "server/gameserver/game_socket_local.hpp"

#include "server/gameserver/socketio/server.hpp"

namespace sbc::server {

boost::asio::awaitable<void> serve_game_socket_local(Request& req, ResponseWriter& w,
                                                     gameserver::socketio::SocketIoServer& hub,
                                                     boost::asio::any_io_executor ex) {
    (void)ex;
    co_await hub.handle_request(req, w);
}

}  // namespace sbc::server
