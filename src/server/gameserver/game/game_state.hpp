#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/gameserver/game/account.hpp"
#include "server/gameserver/game/chatroom.hpp"

namespace sbc::server::gameserver {

// GameState is the shared in-memory game state guarded by a single mutex. Both
// AccountManager and ChatRoomManager operate on it under `mu`, which serializes
// all account/room mutations and removes any cross-manager lock-ordering hazard
// (the original server is single-threaded; this preserves that invariant).
struct GameState {
    std::mutex mu;
    std::unordered_map<std::string, std::shared_ptr<OnlineAccount>> by_socket;   // by socket id
    std::unordered_map<std::int64_t, std::shared_ptr<OnlineAccount>> by_member;  // by MemberNumber
    std::vector<std::shared_ptr<ChatRoom>> rooms;                                 // ChatRoom[]
};

}  // namespace sbc::server::gameserver
