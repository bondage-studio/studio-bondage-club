#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sbc::server::gameserver::socketio {
class Socket;
}

namespace sbc::server::gameserver {

struct ChatRoom;

// OnlineAccount is a logged-in account held in memory, mirroring an entry of the
// original server's `Account[]` array. `data` is the dynamic account object
// (Name, AccountName, MemberNumber, Appearance, FriendList, Ownership, ...); the
// other members are runtime-only state that is never persisted directly.
//
// All access to the mutable members is serialized by AccountManager's mutex.
struct OnlineAccount {
    std::string id;  // socket id (== Engine.IO sid); BC's Account.ID
    std::shared_ptr<socketio::Socket> socket;
    nlohmann::json data;  // persistable account fields + runtime Environment

    // Delayed database writes (flushed every 300s and on disconnect).
    std::optional<nlohmann::json> delayed_appearance;
    std::optional<nlohmann::json> delayed_skill;
    std::optional<nlohmann::json> delayed_game;

    // The chat room the account currently occupies (nullptr if none). Owned by
    // GameState::rooms; this is a non-owning back-reference.
    ChatRoom* chat_room = nullptr;

    std::string account_name() const { return data.value("AccountName", std::string{}); }
    std::int64_t member_number() const {
        return data.contains("MemberNumber") && data["MemberNumber"].is_number_integer()
                   ? data["MemberNumber"].get<std::int64_t>()
                   : 0;
    }
    std::string environment() const { return data.value("Environment", std::string{}); }
};

}  // namespace sbc::server::gameserver
