#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

#include "server/gameserver/game/account.hpp"
#include "server/gameserver/game/game_state.hpp"
#include "server/gameserver/game/settings.hpp"

namespace sbc::net {
class BlockingPool;
}

namespace sbc::server::gameserver {

class GameDb;
class Mailer;
class ChatRoomManager;

namespace socketio {
class Socket;
class SocketIoServer;
}  // namespace socketio

// AccountManager owns account event handlers, the online-account registry, login
// queue, periodic ServerInfo broadcast, and delayed appearance/skill/game flush.
class AccountManager {
public:
    AccountManager(boost::asio::any_io_executor ex, net::BlockingPool& blocking, GameDb& db,
                   socketio::SocketIoServer& hub, Mailer& mailer, GameState& state,
                   const GameSettings& settings);

    // Starts the periodic ServerInfo broadcast and delayed flush timers.
    void start();

    void close();

    // on_socket_connected registers the pre-login event handlers on a freshly
    // connected socket and sends it the initial ServerInfo.
    void on_socket_connected(std::shared_ptr<socketio::Socket> socket);

    std::shared_ptr<OnlineAccount> get_by_socket(const std::string& id);
    std::shared_ptr<OnlineAccount> get_by_member(std::int64_t member_number);

    std::size_t online_count();

    // set_post_login_hook installs a callback invoked (on the connection strand)
    // after a successful login/create — ChatRoomManager uses it to register its
    // post-login event handlers.
    using PostLoginHook = std::function<void(std::shared_ptr<socketio::Socket>,
                                             std::shared_ptr<OnlineAccount>)>;
    void set_post_login_hook(PostLoginHook hook) { post_login_hook_ = std::move(hook); }

    // set_disconnect_hook installs a callback invoked when an account leaves
    // (disconnect or explicit AccountDisconnect), before it is erased — used by
    // ChatRoomManager to remove the player from its room. The hook must do its own
    // locking and must not be called with GameState::mu held.
    using DisconnectHook = std::function<void(std::shared_ptr<OnlineAccount>)>;
    void set_disconnect_hook(DisconnectHook hook) { disconnect_hook_ = std::move(hook); }

    void set_chatrooms(ChatRoomManager* chatrooms) { chatrooms_ = chatrooms; }

private:
    boost::asio::awaitable<void> account_create(std::shared_ptr<socketio::Socket> socket,
                                                nlohmann::json data);
    void enqueue_login(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    boost::asio::awaitable<void> run_login_queue();
    boost::asio::awaitable<void> account_login_process(std::shared_ptr<socketio::Socket> socket,
                                                       std::string upper_name, std::string password);
    void account_update(const std::string& socket_id, nlohmann::json data);
    void account_query(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    void account_beep(const std::string& socket_id, nlohmann::json data);
    void account_difficulty(const std::string& socket_id, nlohmann::json data);
    void account_ownership(const std::string& socket_id, nlohmann::json data);
    void account_lovership(const std::string& socket_id, nlohmann::json data);
    // persist_fields / persist_member_fields write account fields to the DB off
    // the I/O thread (fire-and-forget).
    void persist_fields(const std::string& upper_name, nlohmann::json fields);
    void persist_member_fields(std::int64_t member_number, nlohmann::json fields);
    void account_update_email(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    boost::asio::awaitable<void> password_reset(std::shared_ptr<socketio::Socket> socket,
                                                std::string email);
    boost::asio::awaitable<void> password_reset_process(std::shared_ptr<socketio::Socket> socket,
                                                        nlohmann::json data);
    boost::asio::awaitable<void> account_query_email_status(
        std::shared_ptr<socketio::Socket> socket);

    void on_login(std::shared_ptr<socketio::Socket> socket, std::shared_ptr<OnlineAccount> acc);
    void register_pre_login(std::shared_ptr<socketio::Socket> socket);
    void remove_account(const std::string& socket_id);

    void send_server_info(const std::shared_ptr<socketio::Socket>& socket);
    boost::asio::awaitable<void> server_info_loop();
    boost::asio::awaitable<void> delayed_update_loop();
    void flush_delayed_updates();

    boost::asio::any_io_executor ex_;
    net::BlockingPool& blocking_;
    GameDb& db_;
    socketio::SocketIoServer& hub_;
    Mailer& mailer_;
    GameState& gs_;  // shared game state (mutex + account/room registries)
    const GameSettings& settings_;  // live, COW policy knobs

    // Login queue: [socket, UPPER name, password]. Processed one at a time with
    // login_pace_ms spacing.
    std::mutex login_mu_;
    std::deque<std::tuple<std::shared_ptr<socketio::Socket>, std::string, std::string>> login_queue_;
    bool login_running_ = false;

    boost::asio::steady_timer server_info_timer_;
    boost::asio::steady_timer delayed_timer_;
    bool stopped_ = false;

    // Password-reset state: account name -> reset code, plus a throttle timestamp.
    std::mutex reset_mu_;
    std::unordered_map<std::string, std::string> password_resets_;
    std::int64_t next_password_reset_ = 0;

    // Account-creation rate limit per IP. Entries are intentionally never pruned
    // so the daily total remains cumulative for the process lifetime.
    std::mutex creation_mu_;
    std::vector<std::pair<std::string, std::int64_t>> account_creation_ip_;

    PostLoginHook post_login_hook_;
    DisconnectHook disconnect_hook_;
    ChatRoomManager* chatrooms_ = nullptr;
};

}  // namespace sbc::server::gameserver
