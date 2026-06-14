#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "server/gameserver/game/account.hpp"
#include "server/gameserver/game/chatroom.hpp"
#include "server/gameserver/game/game_state.hpp"
#include "server/gameserver/game/settings.hpp"

namespace sbc::server::gameserver {

class AccountManager;

namespace socketio {
class Socket;
class SocketIoServer;
}  // namespace socketio

// ChatRoomManager ports the chat-room event handlers of the original BC server:
// create / search / join / leave / chat / game / character-update, plus the
// ChatRoomSync* broadcast family. Rooms live only in memory (GameState::rooms).
// All state access is serialized by GameState::mu.
class ChatRoomManager {
public:
    ChatRoomManager(socketio::SocketIoServer& hub, GameState& state, const GameSettings& settings);

    // register_handlers installs the post-login chat-room handlers on a socket.
    // Invoked from AccountManager's post-login hook.
    void register_handlers(std::shared_ptr<socketio::Socket> socket,
                           std::shared_ptr<OnlineAccount> acc);

    // remove_from_room takes the account out of its current room (disconnect /
    // explicit leave). Locks GameState internally.
    void remove_from_room(std::shared_ptr<OnlineAccount> acc);

    std::size_t room_count();

    // Helpers callable by AccountManager (ownership/lovership flows). The caller
    // MUST already hold GameState::mu.
    void broadcast_message(ChatRoom& room, std::int64_t sender, const std::string& content,
                           const std::string& type, const nlohmann::json& target,
                           const nlohmann::json& dictionary);
    void sync_character(ChatRoom& room, std::int64_t source_member, std::int64_t target_member);

private:
    // Handlers (run on the acting connection's strand; each locks GameState::mu).
    void chat_room_search(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    void chat_room_create(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    void chat_room_join(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);
    void chat_room_leave(const std::string& socket_id);
    void chat_room_chat(const std::string& socket_id, nlohmann::json data);
    void chat_room_game(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_expression_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_map_data_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_pose_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_arousal_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_character_item_update(const std::string& socket_id, nlohmann::json data);
    void chat_room_admin(const std::string& socket_id, nlohmann::json data);
    void chat_room_allow_item(std::shared_ptr<socketio::Socket> socket, nlohmann::json data);

    // --- helpers below assume GameState::mu is held by the caller ---
    std::shared_ptr<OnlineAccount> account_for(const std::string& socket_id);
    void room_remove_locked(const std::shared_ptr<OnlineAccount>& acc, const std::string& reason,
                            nlohmann::json dictionary);
    void chat_room_message(ChatRoom& room, std::int64_t sender, const std::string& content,
                           const std::string& type, const nlohmann::json& target,
                           const nlohmann::json& dictionary);
    nlohmann::json char_shared_data(const OnlineAccount& acc, const ChatRoom& room);
    nlohmann::json room_data(const ChatRoom& room, std::int64_t source_member);
    nlohmann::json room_properties(const ChatRoom& room, std::int64_t source_member);
    void sync(const ChatRoom& room, std::int64_t source_member);
    void sync_to_member(const ChatRoom& room, std::int64_t source_member, std::int64_t target_member);
    void sync_member_join(const ChatRoom& room, const OnlineAccount& character);
    void sync_member_leave(const ChatRoom& room, std::int64_t source_member);
    void sync_single(const OnlineAccount& acc, std::int64_t source_member);
    void sync_room_properties(const ChatRoom& room, std::int64_t source_member);
    void sync_reorder_players(const ChatRoom& room, std::int64_t source_member);

    socketio::SocketIoServer& hub_;
    GameState& gs_;
    const GameSettings& settings_;  // live, COW policy knobs
};

}  // namespace sbc::server::gameserver
