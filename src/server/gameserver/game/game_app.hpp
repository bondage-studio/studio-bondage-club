#pragma once

#include <memory>
#include <string>

#include <boost/asio/any_io_executor.hpp>

#include "config/config.hpp"
#include "server/gameserver/game/account_manager.hpp"
#include "server/gameserver/game/chatroom_manager.hpp"
#include "server/gameserver/game/game_db.hpp"
#include "server/gameserver/game/game_state.hpp"
#include "server/gameserver/game/mailer.hpp"
#include "server/gameserver/game/settings.hpp"
#include "server/gameserver/socketio/server.hpp"

namespace sbc::net {
class BlockingPool;
}

namespace sbc::server::gameserver {

// GameApp owns the embedded Socket.IO hub, the LevelDB account store, the shared
// game state, and the game managers. It is a long-lived object held directly by
// App (outside App::State) so toggling the remote/local game server at runtime
// never tears down in-memory sockets/rooms or the on-disk store.
class GameApp {
public:
    GameApp(boost::asio::any_io_executor ex, net::BlockingPool& blocking,
            const std::string& data_dir, const config::GameServerConfig& settings = {});

    socketio::SocketIoServer& hub() { return hub_; }
    AccountManager& accounts() { return *accounts_; }
    ChatRoomManager& chatrooms() { return *chatrooms_; }

    // update_settings swaps the live policy knobs (COW). In-flight connections
    // keep running; the new values are picked up on the next event / tick / hash.
    void update_settings(const config::GameServerConfig& settings);

    // detach_db quiesces the game server for a cache-dir migration: it drops live
    // sockets and releases the LevelDB directory lock so the data dir can be moved
    // on disk. Pair with reopen() to point the store at the migrated location.
    void detach_db();

    // reopen points the account store at a new directory (used when cache.dir,
    // which contains the embedded game DB, is migrated). Call with the hub idle.
    void reopen(const std::string& data_dir);

    // close flushes pending writes and disconnects all live sockets.
    void close();

private:
    GameSettings settings_;  // declared first: hub_/managers reference it
    std::shared_ptr<GameDb> db_;
    LogMailer mailer_;
    GameState gs_;
    socketio::SocketIoServer hub_;
    std::unique_ptr<AccountManager> accounts_;
    std::unique_ptr<ChatRoomManager> chatrooms_;
};

}  // namespace sbc::server::gameserver
