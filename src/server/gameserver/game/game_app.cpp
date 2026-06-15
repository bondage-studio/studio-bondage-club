#include "server/gameserver/game/game_app.hpp"

#include <memory>

#include "server/gameserver/socketio/socket_facade.hpp"

namespace sbc::server::gameserver {

namespace {

// to_limits projects the game settings onto the transport-layer knobs the
// socketio hub needs (keeping the hub free of any config dependency).
socketio::TransportLimits to_limits(const config::GameServerConfig& g) {
    socketio::TransportLimits l;
    l.ping_interval_ms = g.ping_interval_ms;
    l.ping_timeout_ms = g.ping_timeout_ms;
    l.max_payload_bytes = g.max_payload_bytes;
    l.message_rate_per_sec = g.message_rate_per_sec;
    l.ip_connection_limit = g.ip_connection_limit;
    l.ip_connection_rate_per_sec = g.ip_connection_rate_per_sec;
    return l;
}

}  // namespace

GameApp::GameApp(boost::asio::any_io_executor ex, net::BlockingPool& blocking,
                 const std::string& data_dir, const config::GameServerConfig& settings)
    : settings_(settings), db_(GameDb::open(data_dir)), hub_(ex) {
    hub_.set_limits(to_limits(settings));
    accounts_ = std::make_unique<AccountManager>(ex, blocking, *db_, hub_, mailer_, gs_, settings_);
    chatrooms_ = std::make_unique<ChatRoomManager>(hub_, gs_, settings_);
    accounts_->set_chatrooms(chatrooms_.get());

    // After login, ChatRoomManager attaches its event handlers; on disconnect it
    // removes the player from any room before the account is erased.
    accounts_->set_post_login_hook(
        [this](std::shared_ptr<socketio::Socket> socket, std::shared_ptr<OnlineAccount> acc) {
            chatrooms_->register_handlers(std::move(socket), std::move(acc));
        });
    accounts_->set_disconnect_hook([this](std::shared_ptr<OnlineAccount> acc) {
        chatrooms_->remove_from_room(std::move(acc));
    });

    hub_.set_connect_handler([this](std::shared_ptr<socketio::Socket> socket) {
        accounts_->on_socket_connected(std::move(socket));
    });

    accounts_->start();
}

void GameApp::update_settings(const config::GameServerConfig& settings) {
    settings_.store(settings);             // managers read the new snapshot on next use
    hub_.set_limits(to_limits(settings));  // transport picks it up per connection/tick
}

void GameApp::detach_db() {
    hub_.disconnect_all();  // no game requests should be mid-flight during the move
    if (db_) db_->close();  // release the LevelDB lock so the dir can be renamed
}

void GameApp::reopen(const std::string& data_dir) {
    // The account store lives under the cache dir; when that dir is migrated we
    // point the same GameDb at the new path so manager references stay valid.
    if (db_) db_->reopen(data_dir);
}

void GameApp::close() {
    if (accounts_) accounts_->close();
    hub_.disconnect_all();
}

}  // namespace sbc::server::gameserver
