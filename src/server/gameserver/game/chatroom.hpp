#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/gameserver/game/account.hpp"

namespace sbc::server::gameserver {

// ChatRoom is an in-memory chat room. `data` holds the room properties (Name,
// Description, Background, Visibility, Access, Admin, Ban, Whitelist, Limit,
// ...) and `accounts` is the ordered member list the clients render.
struct ChatRoom {
    std::string id;  // base64-style room id; the socket.io room is "chatroom-"+id
    nlohmann::json data;
    std::vector<std::shared_ptr<OnlineAccount>> accounts;

    std::string name() const { return data.value("Name", std::string{}); }
    std::string environment() const { return data.value("Environment", std::string{}); }
    std::int64_t limit() const {
        return data.contains("Limit") && data["Limit"].is_number_integer()
                   ? data["Limit"].get<std::int64_t>()
                   : 0;
    }
    std::string socket_room() const { return "chatroom-" + id; }
};

}  // namespace sbc::server::gameserver
