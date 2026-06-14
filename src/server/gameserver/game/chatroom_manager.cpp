#include "server/gameserver/game/chatroom_manager.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <regex>

#include "server/gameserver/engineio/eio_protocol.hpp"
#include "server/gameserver/socketio/server.hpp"
#include "server/gameserver/socketio/socket_facade.hpp"

namespace sbc::server::gameserver {

using json = nlohmann::json;

namespace {

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string upper_trim(std::string s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t");
    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

bool has_str(const json& arr, const char* s) {
    if (!arr.is_array()) return false;
    for (const auto& e : arr)
        if (e.is_string() && e.get<std::string>() == s) return true;
    return false;
}

bool has_member(const json& arr, std::int64_t n) {
    if (!arr.is_array()) return false;
    for (const auto& e : arr)
        if (e.is_number_integer() && e.get<std::int64_t>() == n) return true;
    return false;
}

bool account_has_any_role(const OnlineAccount& acc, const ChatRoom& room, const json& roles) {
    if (!roles.is_array()) return false;
    if (has_str(roles, "All")) return true;
    std::int64_t m = acc.member_number();
    if (has_str(roles, "Admin") && has_member(room.data.value("Admin", json::array()), m)) return true;
    if (has_str(roles, "Whitelist") && has_member(room.data.value("Whitelist", json::array()), m))
        return true;
    return false;
}

bool should_send_blacklist(const OnlineAccount& acc) {
    std::int64_t p = acc.data.value("ItemPermission", 2);
    return p == 1 || p == 2;
}

std::int64_t dominant_value(const OnlineAccount& acc) {
    if (acc.data.contains("Reputation") && acc.data["Reputation"].is_array()) {
        for (const auto& rep : acc.data["Reputation"]) {
            if (rep.is_object() && rep.value("Type", std::string{}) == "Dominant" &&
                rep.contains("Value") && rep["Value"].is_number()) {
                return rep["Value"].get<std::int64_t>();
            }
        }
    }
    return 0;
}

std::vector<std::int64_t> lover_numbers(const OnlineAccount& acc) {
    std::vector<std::int64_t> out;
    if (acc.data.contains("Lovership") && acc.data["Lovership"].is_array())
        for (const auto& l : acc.data["Lovership"])
            if (l.is_object() && l.contains("MemberNumber") && l["MemberNumber"].is_number_integer())
                out.push_back(l["MemberNumber"].get<std::int64_t>());
    return out;
}

bool get_allow_item(const OnlineAccount& source, const OnlineAccount& target) {
    std::int64_t sm = source.member_number();
    std::int64_t perm = target.data.value("ItemPermission", 2);
    json owner = target.data.value("Ownership", json{});
    bool owned_by_source = owner.is_object() && owner.contains("MemberNumber") &&
                           owner["MemberNumber"].is_number_integer() &&
                           owner["MemberNumber"].get<std::int64_t>() == sm;
    if (perm <= 0 || sm == target.member_number() || owned_by_source) return true;

    json black = target.data.value("BlackList", json::array());
    json white = target.data.value("WhiteList", json::array());
    auto lovers = lover_numbers(target);
    bool is_lover = std::find(lovers.begin(), lovers.end(), sm) != lovers.end();

    if (perm == 1 && !has_member(black, sm)) return true;
    if (perm == 2 && !has_member(black, sm) &&
        (dominant_value(source) + 25 >= dominant_value(target) || has_member(white, sm) || is_lover))
        return true;
    if (perm == 3 && (has_member(white, sm) || is_lover)) return true;
    if (perm == 4 && is_lover) return true;
    return false;
}

void copy_if_present(json& dst, const json& src, const char* key) {
    if (src.contains(key)) dst[key] = src[key];
}

}  // namespace

ChatRoomManager::ChatRoomManager(socketio::SocketIoServer& hub, GameState& state,
                                 const GameSettings& settings)
    : hub_(hub), gs_(state), settings_(settings) {}

std::size_t ChatRoomManager::room_count() {
    std::lock_guard<std::mutex> lock(gs_.mu);
    return gs_.rooms.size();
}

void ChatRoomManager::register_handlers(std::shared_ptr<socketio::Socket> socket,
                                        std::shared_ptr<OnlineAccount> acc) {
    (void)acc;
    std::string id = socket->id();
    socket->on("ChatRoomSearch",
               [this, socket](const json& data) { chat_room_search(socket, data); });
    socket->on("ChatRoomCreate",
               [this, socket](const json& data) { chat_room_create(socket, data); });
    socket->on("ChatRoomJoin",
               [this, socket](const json& data) { chat_room_join(socket, data); });
    socket->on("ChatRoomLeave", [this, id](const json&) { chat_room_leave(id); });
    socket->on("ChatRoomChat", [this, id](const json& data) { chat_room_chat(id, data); });
    socket->on("ChatRoomGame", [this, id](const json& data) { chat_room_game(id, data); });
    socket->on("ChatRoomCharacterUpdate",
               [this, id](const json& data) { chat_room_character_update(id, data); });
    socket->on("ChatRoomCharacterExpressionUpdate",
               [this, id](const json& data) { chat_room_character_expression_update(id, data); });
    socket->on("ChatRoomCharacterMapDataUpdate",
               [this, id](const json& data) { chat_room_character_map_data_update(id, data); });
    socket->on("ChatRoomCharacterPoseUpdate",
               [this, id](const json& data) { chat_room_character_pose_update(id, data); });
    socket->on("ChatRoomCharacterArousalUpdate",
               [this, id](const json& data) { chat_room_character_arousal_update(id, data); });
    socket->on("ChatRoomCharacterItemUpdate",
               [this, id](const json& data) { chat_room_character_item_update(id, data); });
    socket->on("ChatRoomAdmin", [this, id](const json& data) { chat_room_admin(id, data); });
    socket->on("ChatRoomAllowItem",
               [this, socket](const json& data) { chat_room_allow_item(socket, data); });
}

std::shared_ptr<OnlineAccount> ChatRoomManager::account_for(const std::string& socket_id) {
    auto it = gs_.by_socket.find(socket_id);
    return it == gs_.by_socket.end() ? nullptr : it->second;
}

nlohmann::json ChatRoomManager::char_shared_data(const OnlineAccount& acc, const ChatRoom& room) {
    json c = json::object();
    for (const char* k :
         {"ID", "Name", "AssetFamily", "Title", "Nickname", "Appearance", "ActivePose", "Reputation",
          "Creation", "Lovership", "Description", "Owner", "MemberNumber", "LabelColor",
          "ItemPermission", "InventoryData", "Ownership", "BlockItems", "LimitedItems",
          "FavoriteItems", "ArousalSettings", "OnlineSharedSettings", "Game", "MapData", "Crafting",
          "Difficulty"}) {
        copy_if_present(c, acc.data, k);
    }
    // WhiteList/BlackList filtered to members currently in the room.
    json white = json::array();
    json black = json::array();
    bool send_black = should_send_blacklist(acc);
    json acc_white = acc.data.value("WhiteList", json::array());
    json acc_black = acc.data.value("BlackList", json::array());
    for (const auto& b : room.accounts) {
        std::int64_t bm = b->member_number();
        if (has_member(acc_white, bm)) white.push_back(bm);
        if (send_black && has_member(acc_black, bm)) black.push_back(bm);
    }
    c["WhiteList"] = white;
    c["BlackList"] = black;
    return c;
}

nlohmann::json ChatRoomManager::room_data(const ChatRoom& room, std::int64_t source_member) {
    json r = room_properties(room, source_member);
    json chars = json::array();
    for (const auto& a : room.accounts) chars.push_back(char_shared_data(*a, room));
    r["Character"] = chars;
    return r;
}

nlohmann::json ChatRoomManager::room_properties(const ChatRoom& room, std::int64_t source_member) {
    json r = json::object();
    for (const char* k : {"Name", "Language", "Description", "Admin", "Whitelist", "Ban", "Background",
                          "Custom", "Limit", "Game", "Visibility", "Access", "Private", "Locked",
                          "MapData", "BlockCategory", "Space"}) {
        copy_if_present(r, room.data, k);
    }
    r["SourceMemberNumber"] = source_member;
    return r;
}

void ChatRoomManager::sync(const ChatRoom& room, std::int64_t source_member) {
    hub_.emit_to_room(room.socket_room(), "ChatRoomSync", room_data(room, source_member));
}

void ChatRoomManager::sync_to_member(const ChatRoom& room, std::int64_t source_member,
                                     std::int64_t target_member) {
    for (const auto& a : room.accounts) {
        if (a->member_number() == target_member) {
            if (a->socket) a->socket->emit("ChatRoomSync", room_data(room, source_member));
            return;
        }
    }
}

void ChatRoomManager::sync_member_join(const ChatRoom& room, const OnlineAccount& character) {
    json join_data;
    join_data["SourceMemberNumber"] = character.member_number();
    join_data["Character"] = char_shared_data(character, room);
    json white_by = json::array();
    json black_by = json::array();
    std::int64_t cm = character.member_number();
    for (const auto& b : room.accounts) {
        if (has_member(b->data.value("WhiteList", json::array()), cm))
            white_by.push_back(b->member_number());
        if (should_send_blacklist(*b) && has_member(b->data.value("BlackList", json::array()), cm))
            black_by.push_back(b->member_number());
    }
    join_data["WhiteListedBy"] = white_by;
    join_data["BlackListedBy"] = black_by;
    // Broadcast to the room except the joining character...
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncMemberJoin", join_data, character.id);
    // ...then send the full room sync to the joiner.
    sync_to_member(room, character.member_number(), character.member_number());
}

void ChatRoomManager::sync_member_leave(const ChatRoom& room, std::int64_t source_member) {
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncMemberLeave",
                      json{{"SourceMemberNumber", source_member}});
}

void ChatRoomManager::sync_single(const OnlineAccount& acc, std::int64_t source_member) {
    if (!acc.chat_room) return;
    json r = {{"SourceMemberNumber", source_member}, {"Character", char_shared_data(acc, *acc.chat_room)}};
    hub_.emit_to_room(acc.chat_room->socket_room(), "ChatRoomSyncSingle", r);
}

void ChatRoomManager::chat_room_message(ChatRoom& room, std::int64_t sender,
                                        const std::string& content, const std::string& type,
                                        const json& target, const json& dictionary) {
    json msg = {{"Sender", sender}, {"Content", content}, {"Type", type}, {"Dictionary", dictionary}};
    if (target.is_null()) {
        hub_.emit_to_room(room.socket_room(), "ChatRoomMessage", msg);
    } else if (target.is_number_integer()) {
        std::int64_t tm = target.get<std::int64_t>();
        for (const auto& a : room.accounts)
            if (a->member_number() == tm) {
                if (a->socket) a->socket->emit("ChatRoomMessage", msg);
                return;
            }
    }
}

void ChatRoomManager::room_remove_locked(const std::shared_ptr<OnlineAccount>& acc,
                                          const std::string& reason, json dictionary) {
    if (!acc->chat_room) return;
    ChatRoom* room = acc->chat_room;
    std::string room_socket = room->socket_room();
    std::int64_t member = acc->member_number();

    if (acc->socket) acc->socket->leave(room_socket);
    auto& members = room->accounts;
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](const std::shared_ptr<OnlineAccount>& a) { return a->id == acc->id; }),
                  members.end());

    if (members.empty()) {
        std::string rid = room->id;
        acc->chat_room = nullptr;
        gs_.rooms.erase(std::remove_if(gs_.rooms.begin(), gs_.rooms.end(),
                                       [&](const std::shared_ptr<ChatRoom>& r) { return r->id == rid; }),
                        gs_.rooms.end());
        return;
    }

    if (!dictionary.is_array() || dictionary.empty()) {
        dictionary = json::array();
        dictionary.push_back(
            json{{"Tag", "SourceCharacter"}, {"Text", acc->data.value("Name", std::string{})},
                 {"MemberNumber", member}});
    }
    chat_room_message(*room, member, reason, "Action", json(nullptr), dictionary);
    sync_member_leave(*room, member);
    acc->chat_room = nullptr;
}

void ChatRoomManager::remove_from_room(std::shared_ptr<OnlineAccount> acc) {
    std::lock_guard<std::mutex> lock(gs_.mu);
    room_remove_locked(acc, "ServerDisconnect", json::array());
}

void ChatRoomManager::chat_room_search(std::shared_ptr<socketio::Socket> socket, json data) {
    if (!data.is_object() || !data.contains("Query") || !data["Query"].is_string() ||
        data["Query"].get<std::string>().size() > 20)
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket->id());
    if (!acc) return;

    std::string query = trim(data["Query"]);
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<std::string> spaces;
    if (data.contains("Space") && data["Space"].is_string())
        spaces.push_back(data["Space"]);
    else if (data.contains("Space") && data["Space"].is_array())
        for (const auto& s : data["Space"])
            if (s.is_string()) spaces.push_back(s);
    std::string game = (data.contains("Game") && data["Game"].is_string()) ? data["Game"] : "";
    bool full_rooms = data.value("FullRooms", false);
    bool show_locked = data.value("ShowLocked", false);
    bool search_descs = data.value("SearchDescs", false);

    json result = json::array();
    // Most-recently-created first.
    for (auto it = gs_.rooms.rbegin(); it != gs_.rooms.rend(); ++it) {
        const ChatRoom& room = **it;
        std::string room_name = upper_trim(room.name());
        if (acc->environment() != room.environment()) continue;
        if (!game.empty() && room.data.value("Game", std::string{}) != game) continue;
        if (static_cast<std::int64_t>(room.accounts.size()) >= room.limit() && !full_rooms) continue;
        if (!spaces.empty() &&
            std::find(spaces.begin(), spaces.end(), room.data.value("Space", std::string{})) ==
                spaces.end())
            continue;
        if (has_member(room.data.value("Ban", json::array()), acc->member_number())) continue;
        if (!query.empty()) {
            bool match = room_name.find(query) != std::string::npos;
            if (!match && search_descs) {
                std::string desc = room.data.value("Description", std::string{});
                std::transform(desc.begin(), desc.end(), desc.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                match = desc.find(query) != std::string::npos;
            }
            if (!match) continue;
        }
        if (room_name != query &&
            !account_has_any_role(*acc, room, room.data.value("Visibility", json::array())))
            continue;
        if (!show_locked &&
            !account_has_any_role(*acc, room, room.data.value("Access", json::array())))
            continue;

        json r = json::object();
        for (const char* k : {"Name", "Language", "Creator", "CreatorMemberNumber", "Creation",
                              "Description", "BlockCategory", "Game", "Space", "Visibility", "Access",
                              "Private", "Locked"}) {
            copy_if_present(r, room.data, k);
        }
        r["MemberCount"] = static_cast<std::int64_t>(room.accounts.size());
        r["MemberLimit"] = room.limit();
        r["Friends"] = json::array();
        r["CanJoin"] = account_has_any_role(*acc, room, room.data.value("Access", json::array()));
        result.push_back(r);
        if (static_cast<int>(result.size()) >= settings_.snapshot()->search_max_results) break;
    }
    socket->emit("ChatRoomSearchResult", result);
}

void ChatRoomManager::chat_room_create(std::shared_ptr<socketio::Socket> socket, json data) {
    auto invalid = [&]() { socket->emit("ChatRoomCreateResponse", "InvalidRoomData"); };
    if (!data.is_object() || !data.contains("Name") || !data["Name"].is_string() ||
        !data.contains("Description") || !data["Description"].is_string() ||
        !data.contains("Background") || !data["Background"].is_string()) {
        invalid();
        return;
    }

    // Backward-compat: Visibility<->Private and Access<->Locked.
    bool has_visibility = data.contains("Visibility") && data["Visibility"].is_array();
    bool has_private = data.contains("Private") && data["Private"].is_boolean();
    if (has_visibility == has_private) {
        invalid();
        return;
    }
    if (has_visibility)
        data["Private"] = !has_str(data["Visibility"], "All");
    else
        data["Visibility"] = data["Private"].get<bool>() ? json::array({"Admin"}) : json::array({"All"});

    bool has_access = data.contains("Access") && data["Access"].is_array();
    bool has_locked = data.contains("Locked") && data["Locked"].is_boolean();
    if (has_access && has_locked) {
        invalid();
        return;
    } else if (has_access) {
        data["Locked"] = !has_str(data["Access"], "All");
    } else if (has_locked) {
        data["Access"] = data["Locked"].get<bool>() ? json::array({"Admin", "Whitelist"})
                                                     : json::array({"All"});
    } else {
        data["Locked"] = false;
        data["Access"] = json::array({"All"});
    }

    std::string name = trim(data["Name"]);
    data["Name"] = name;
    auto cfg = settings_.snapshot();
    static const std::regex name_re(R"(^[\x20-\x7E]{1,20}$)");
    if (!std::regex_match(name, name_re) ||
        static_cast<int>(data["Description"].get<std::string>().size()) > cfg->description_max_len ||
        data["Background"].get<std::string>().size() > 100) {
        invalid();
        return;
    }

    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket->id());
    if (!acc) {
        socket->emit("ChatRoomCreateResponse", "AccountError");
        return;
    }
    std::string upper_name = upper_trim(name);
    for (const auto& r : gs_.rooms)
        if (upper_trim(r->name()) == upper_name) {
            socket->emit("ChatRoomCreateResponse", "RoomAlreadyExist");
            return;
        }

    std::string space = (data.contains("Space") && data["Space"].is_string()) ? data["Space"] : "";
    std::string game = (data.contains("Game") && data["Game"].is_string()) ? data["Game"] : "";
    if (!data.contains("BlockCategory") || !data["BlockCategory"].is_array())
        data["BlockCategory"] = json::array();
    if (!data.contains("Ban") || !data["Ban"].is_array()) data["Ban"] = json::array();
    if (!data.contains("Whitelist") || !data["Whitelist"].is_array()) data["Whitelist"] = json::array();
    if (!data.contains("Admin") || !data["Admin"].is_array())
        data["Admin"] = json::array({acc->member_number()});

    std::int64_t limit = data.contains("Limit") && data["Limit"].is_number_integer()
                             ? data["Limit"].get<std::int64_t>()
                             : cfg->room_limit_default;
    if (limit < cfg->room_limit_min || limit > cfg->room_limit_max) limit = cfg->room_limit_default;

    room_remove_locked(acc, "ServerLeave", json::array());

    auto room = std::make_shared<ChatRoom>();
    room->id = engineio::generate_sid();
    room->data = json::object();
    room->data["Name"] = name;
    if (data.contains("Language")) room->data["Language"] = data["Language"];
    room->data["Description"] = data["Description"];
    room->data["Background"] = data["Background"];
    if (data.contains("Custom")) room->data["Custom"] = data["Custom"];
    room->data["Limit"] = limit;
    room->data["Visibility"] = data["Visibility"];
    room->data["Access"] = data["Access"];
    room->data["Private"] = data.value("Private", false);
    room->data["Locked"] = data.value("Locked", false);
    if (data.contains("MapData")) room->data["MapData"] = data["MapData"];
    room->data["Environment"] = acc->environment();
    room->data["Space"] = space;
    room->data["Game"] = game;
    room->data["Creator"] = acc->data.value("Name", std::string{});
    room->data["CreatorMemberNumber"] = acc->member_number();
    room->data["Creation"] = now_ms();
    room->data["Ban"] = data["Ban"];
    room->data["BlockCategory"] = data["BlockCategory"];
    room->data["Whitelist"] = data["Whitelist"];
    room->data["Admin"] = data["Admin"];

    gs_.rooms.push_back(room);
    acc->chat_room = room.get();
    room->accounts.push_back(acc);
    if (acc->socket) acc->socket->join(room->socket_room());
    socket->emit("ChatRoomCreateResponse", "ChatRoomCreated");
    sync(*room, acc->member_number());
}

void ChatRoomManager::chat_room_join(std::shared_ptr<socketio::Socket> socket, json data) {
    if (!data.is_object() || !data.contains("Name") || !data["Name"].is_string() ||
        data["Name"].get<std::string>().empty()) {
        socket->emit("ChatRoomSearchResponse", "InvalidRoomData");
        return;
    }
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket->id());
    if (!acc) {
        socket->emit("ChatRoomSearchResponse", "AccountError");
        return;
    }
    std::string want = upper_trim(data["Name"]);
    for (const auto& room_ptr : gs_.rooms) {
        ChatRoom& room = *room_ptr;
        if (upper_trim(room.name()) != want) continue;
        if (acc->environment() != room.environment()) break;
        if (static_cast<std::int64_t>(room.accounts.size()) >= room.limit()) {
            socket->emit("ChatRoomSearchResponse", "RoomFull");
            return;
        }
        if (has_member(room.data.value("Ban", json::array()), acc->member_number())) {
            socket->emit("ChatRoomSearchResponse", "RoomBanned");
            return;
        }
        if (!account_has_any_role(*acc, room, room.data.value("Access", json::array()))) {
            socket->emit("ChatRoomSearchResponse", "RoomLocked");
            return;
        }
        if (acc->chat_room && acc->chat_room->id == room.id) {
            socket->emit("ChatRoomSearchResponse", "AlreadyInRoom");
            return;
        }
        room_remove_locked(acc, "ServerLeave", json::array());
        acc->chat_room = &room;
        room.accounts.push_back(acc);
        if (acc->socket) acc->socket->join(room.socket_room());
        socket->emit("ChatRoomSearchResponse", "JoinedRoom");
        sync_member_join(room, *acc);
        chat_room_message(room, acc->member_number(), "ServerEnter", "Action", json(nullptr),
                          json::array({json{{"Tag", "SourceCharacter"},
                                            {"Text", acc->data.value("Name", std::string{})},
                                            {"MemberNumber", acc->member_number()}}}));
        return;
    }
    socket->emit("ChatRoomSearchResponse", "CannotFindRoom");
}

void ChatRoomManager::chat_room_leave(const std::string& socket_id) {
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (acc) room_remove_locked(acc, "ServerLeave", json::array());
}

void ChatRoomManager::chat_room_chat(const std::string& socket_id, json data) {
    static const std::vector<std::string> kTypes = {"Chat", "Action", "Activity", "Emote",
                                                    "Whisper", "Hidden", "Status"};
    if (!data.is_object() || !data.contains("Content") || !data["Content"].is_string() ||
        !data.contains("Type") || !data["Type"].is_string())
        return;
    std::string type = data["Type"];
    if (std::find(kTypes.begin(), kTypes.end(), type) == kTypes.end()) return;
    if (data["Content"].get<std::string>().size() > 2000) return;

    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    json target = data.contains("Target") ? data["Target"] : json(nullptr);
    json dict = data.contains("Dictionary") ? data["Dictionary"] : json(nullptr);
    chat_room_message(*acc->chat_room, acc->member_number(), trim(data["Content"]), type, target,
                      dict);
}

void ChatRoomManager::chat_room_game(const std::string& socket_id, json data) {
    if (!data.is_object()) return;
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double r = dist(rng);
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    hub_.emit_to_room(acc->chat_room->socket_room(), "ChatRoomGameResponse",
                      json{{"Sender", acc->member_number()}, {"Data", data}, {"RNG", r}});
}

void ChatRoomManager::chat_room_character_update(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("ID") || !data["ID"].is_string() ||
        data["ID"].get<std::string>().empty() || !data.contains("Appearance") ||
        !data["Appearance"].is_array())
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    ChatRoom& room = *acc->chat_room;
    if (has_member(room.data.value("Ban", json::array()), acc->member_number())) return;
    std::string target_id = data["ID"];
    for (const auto& room_acc : room.accounts) {
        if (room_acc->id != target_id) continue;
        if (!get_allow_item(*acc, *room_acc)) return;
        room_acc->delayed_appearance = data["Appearance"];
        room_acc->data["Appearance"] = data["Appearance"];
        if (data.contains("ActivePose")) room_acc->data["ActivePose"] = data["ActivePose"];
        sync_single(*room_acc, acc->member_number());
        return;
    }
}

void ChatRoomManager::broadcast_message(ChatRoom& room, std::int64_t sender,
                                        const std::string& content, const std::string& type,
                                        const json& target, const json& dictionary) {
    chat_room_message(room, sender, content, type, target, dictionary);
}

// ChatRoomSyncCharacter: sends the target's shared data to everyone in the room
// except the source.
void ChatRoomManager::sync_character(ChatRoom& room, std::int64_t source_member,
                                     std::int64_t target_member) {
    std::shared_ptr<OnlineAccount> target, source;
    for (const auto& a : room.accounts) {
        if (a->member_number() == target_member) target = a;
        if (a->member_number() == source_member) source = a;
    }
    if (!target || !source) return;
    json cd = {{"SourceMemberNumber", source_member}, {"Character", char_shared_data(*target, room)}};
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncCharacter", cd, source->id);
}

void ChatRoomManager::sync_room_properties(const ChatRoom& room, std::int64_t source_member) {
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncRoomProperties",
                      room_properties(room, source_member));
}

void ChatRoomManager::sync_reorder_players(const ChatRoom& room, std::int64_t source_member) {
    (void)source_member;
    json order = json::array();
    for (const auto& a : room.accounts) order.push_back(a->member_number());
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncReorderPlayers", json{{"PlayerOrder", order}});
}

namespace {
// Builds a one-entry dictionary describing the acting source character.
json source_dict(const OnlineAccount& acc) {
    return json::array({json{{"Tag", "SourceCharacter"},
                             {"Text", acc.data.value("Name", std::string{})},
                             {"MemberNumber", acc.member_number()}}});
}
bool role_list_restrictive(const json& roles) { return !has_str(roles, "All"); }
}  // namespace

void ChatRoomManager::chat_room_character_expression_update(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("Group") || !data["Group"].is_string() ||
        data["Group"].get<std::string>().empty())
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc) return;
    if (data.contains("Appearance") && data["Appearance"].is_array() && data["Appearance"].size() >= 5)
        acc->data["Appearance"] = data["Appearance"];
    if (!acc->chat_room) return;
    json out = {{"MemberNumber", acc->member_number()}, {"Group", data["Group"]}};
    if (data.contains("Name")) out["Name"] = data["Name"];
    hub_.emit_to_room(acc->chat_room->socket_room(), "ChatRoomSyncExpression", out, acc->id);
}

void ChatRoomManager::chat_room_character_map_data_update(const std::string& socket_id, json data) {
    if (!data.is_object()) return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    acc->data["MapData"] = data;
    hub_.emit_to_room(acc->chat_room->socket_room(), "ChatRoomSyncMapData",
                      json{{"MemberNumber", acc->member_number()}, {"MapData", data}}, acc->id);
}

void ChatRoomManager::chat_room_character_pose_update(const std::string& socket_id, json data) {
    if (!data.is_object()) return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc) return;
    json pose = json::array();
    if (data.contains("Pose")) {
        if (data["Pose"].is_array()) {
            for (const auto& p : data["Pose"])
                if (p.is_string()) pose.push_back(p);
        } else if (data["Pose"].is_string()) {
            pose.push_back(data["Pose"]);
        }
    }
    acc->data["ActivePose"] = pose;
    if (!acc->chat_room) return;
    hub_.emit_to_room(acc->chat_room->socket_room(), "ChatRoomSyncPose",
                      json{{"MemberNumber", acc->member_number()}, {"Pose", pose}}, acc->id);
}

void ChatRoomManager::chat_room_character_arousal_update(const std::string& socket_id, json data) {
    if (!data.is_object()) return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->data.contains("ArousalSettings") || !acc->data["ArousalSettings"].is_object())
        return;
    auto& as = acc->data["ArousalSettings"];
    for (const char* k : {"OrgasmTimer", "OrgasmCount", "Progress", "ProgressTimer"})
        if (data.contains(k)) as[k] = data[k];
    if (!acc->chat_room) return;
    json out = {{"MemberNumber", acc->member_number()}};
    for (const char* k : {"OrgasmTimer", "OrgasmCount", "Progress", "ProgressTimer"})
        if (data.contains(k)) out[k] = data[k];
    hub_.emit_to_room(acc->chat_room->socket_room(), "ChatRoomSyncArousal", out, acc->id);
}

void ChatRoomManager::chat_room_character_item_update(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("Target") || !data["Target"].is_number_integer() ||
        !data.contains("Group") || !data["Group"].is_string())
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    ChatRoom& room = *acc->chat_room;
    if (has_member(room.data.value("Ban", json::array()), acc->member_number())) return;
    std::int64_t target = data["Target"].get<std::int64_t>();
    for (const auto& room_acc : room.accounts)
        if (room_acc->member_number() == target && !get_allow_item(*acc, *room_acc)) return;
    hub_.emit_to_room(room.socket_room(), "ChatRoomSyncItem",
                      json{{"Source", acc->member_number()}, {"Item", data}}, acc->id);
}

void ChatRoomManager::chat_room_admin(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("MemberNumber") || !data["MemberNumber"].is_number_integer() ||
        !data.contains("Action") || !data["Action"].is_string())
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket_id);
    if (!acc || !acc->chat_room) return;
    ChatRoom& room = *acc->chat_room;
    std::int64_t me = acc->member_number();
    if (!has_member(room.data.value("Admin", json::array()), me)) return;
    std::string action = data["Action"];
    std::int64_t target_member = data["MemberNumber"].get<std::int64_t>();

    // Only Swap/MoveLeft/MoveRight may target the admin themselves.
    if (me == target_member && action != "Swap" && action != "MoveLeft" && action != "MoveRight")
        return;

    if (action == "Update") {
        if (!data.contains("Room") || !data["Room"].is_object()) {
            acc->socket->emit("ChatRoomUpdateResponse", "InvalidRoomData");
            return;
        }
        json r = data["Room"];
        if (!r.contains("Name") || !r["Name"].is_string() || !r.contains("Description") ||
            !r["Description"].is_string() || !r.contains("Background") || !r["Background"].is_string() ||
            !r.contains("Admin") || !r["Admin"].is_array() || !r.contains("Ban") ||
            !r["Ban"].is_array()) {
            acc->socket->emit("ChatRoomUpdateResponse", "InvalidRoomData");
            return;
        }
        std::string new_name = trim(r["Name"]);
        auto cfg = settings_.snapshot();
        static const std::regex name_re(R"(^[\x20-\x7E]{1,20}$)");
        if (!std::regex_match(new_name, name_re) ||
            static_cast<int>(r["Description"].get<std::string>().size()) > cfg->description_max_len ||
            r["Background"].get<std::string>().size() > 100) {
            acc->socket->emit("ChatRoomUpdateResponse", "InvalidRoomData");
            return;
        }
        std::string upper_new = upper_trim(new_name);
        for (const auto& other : gs_.rooms)
            if (upper_trim(room.name()) != new_name && other->id != room.id &&
                upper_trim(other->name()) == upper_new) {
                acc->socket->emit("ChatRoomUpdateResponse", "RoomAlreadyExist");
                return;
            }
        // Backward-compat Visibility<->Private, Access<->Locked.
        if (r.contains("Visibility") && r["Visibility"].is_array())
            r["Private"] = !has_str(r["Visibility"], "All");
        else if (r.contains("Private") && r["Private"].is_boolean())
            r["Visibility"] = r["Private"].get<bool>() ? json::array({"Admin"}) : json::array({"All"});
        if (r.contains("Access") && r["Access"].is_array())
            r["Locked"] = !has_str(r["Access"], "All");
        else if (r.contains("Locked") && r["Locked"].is_boolean())
            r["Access"] = r["Locked"].get<bool>() ? json::array({"Admin", "Whitelist"})
                                                  : json::array({"All"});

        room.data["Name"] = new_name;
        if (r.contains("Language")) room.data["Language"] = r["Language"];
        room.data["Background"] = r["Background"];
        if (r.contains("Custom")) room.data["Custom"] = r["Custom"];
        room.data["Description"] = r["Description"];
        room.data["BlockCategory"] =
            (r.contains("BlockCategory") && r["BlockCategory"].is_array()) ? r["BlockCategory"]
                                                                          : json::array();
        room.data["Ban"] = r["Ban"];
        if (r.contains("Whitelist") && r["Whitelist"].is_array()) room.data["Whitelist"] = r["Whitelist"];
        room.data["Admin"] = r["Admin"];
        room.data["Game"] = (r.contains("Game") && r["Game"].is_string() &&
                             r["Game"].get<std::string>().size() <= 100)
                                ? r["Game"]
                                : json("");
        std::int64_t limit = r.contains("Limit") && r["Limit"].is_number_integer()
                                 ? r["Limit"].get<std::int64_t>()
                                 : cfg->room_limit_default;
        if (limit < cfg->room_limit_min || limit > cfg->room_limit_max)
            limit = cfg->room_limit_default;
        room.data["Limit"] = limit;
        if (r.contains("Visibility") && r["Visibility"].is_array()) room.data["Visibility"] = r["Visibility"];
        if (r.contains("Access") && r["Access"].is_array()) room.data["Access"] = r["Access"];
        if (r.contains("Private") && r["Private"].is_boolean()) room.data["Private"] = r["Private"];
        if (r.contains("Locked") && r["Locked"].is_boolean()) room.data["Locked"] = r["Locked"];
        if (r.contains("MapData")) room.data["MapData"] = r["MapData"];
        acc->socket->emit("ChatRoomUpdateResponse", "Updated");
        json dict = json::array();
        dict.push_back(json{{"Tag", "SourceCharacter"}, {"Text", acc->data.value("Name", std::string{})},
                            {"MemberNumber", me}});
        dict.push_back(json{{"Tag", "ChatRoomName"}, {"Text", room.name()}});
        dict.push_back(json{{"Tag", "ChatRoomLimit"}, {"Text", std::to_string(room.limit())}});
        dict.push_back(json{{"Tag", "ChatRoomPrivacy"},
                            {"TextToLookUp", role_list_restrictive(room.data.value("Visibility", json::array()))
                                                 ? "Private"
                                                 : "Public"}});
        dict.push_back(json{{"Tag", "ChatRoomLocked"},
                            {"TextToLookUp", role_list_restrictive(room.data.value("Access", json::array()))
                                                 ? "Locked"
                                                 : "Unlocked"}});
        chat_room_message(room, me, "ServerUpdateRoom", "Action", json(nullptr), dict);
        sync_room_properties(room, me);
        return;
    }

    if (action == "Swap" && data.contains("TargetMemberNumber") &&
        data["TargetMemberNumber"].is_number_integer() && data.contains("DestinationMemberNumber") &&
        data["DestinationMemberNumber"].is_number_integer() &&
        data["TargetMemberNumber"] != data["DestinationMemberNumber"]) {
        std::int64_t tm = data["TargetMemberNumber"].get<std::int64_t>();
        std::int64_t dm = data["DestinationMemberNumber"].get<std::int64_t>();
        auto& members = room.accounts;
        auto ti = std::find_if(members.begin(), members.end(),
                               [&](const auto& a) { return a->member_number() == tm; });
        auto di = std::find_if(members.begin(), members.end(),
                               [&](const auto& a) { return a->member_number() == dm; });
        if (ti == members.end() || di == members.end()) return;
        std::iter_swap(ti, di);
        sync_reorder_players(room, me);
        chat_room_message(room, me, "ServerSwap", "Action", json(nullptr), source_dict(*acc));
        return;
    }

    // Actions on a member currently in the room.
    auto& members = room.accounts;
    for (std::size_t a = 0; a < members.size(); ++a) {
        if (members[a]->member_number() != target_member) continue;
        auto target = members[a];
        json dict = json::array();
        auto tgt_dict = [&]() {
            dict.push_back(json{{"Tag", "TargetCharacterName"},
                                {"Text", target->data.value("Name", std::string{})},
                                {"MemberNumber", target->member_number()}});
            dict.push_back(json{{"Tag", "SourceCharacter"},
                                {"Text", acc->data.value("Name", std::string{})},
                                {"MemberNumber", me}});
        };
        if (action == "Ban") {
            room.data["Ban"].push_back(target_member);
            if (target->socket) target->socket->emit("ChatRoomSearchResponse", "RoomBanned");
            dict.push_back(json{{"Tag", "SourceCharacter"},
                                {"Text", acc->data.value("Name", std::string{})}, {"MemberNumber", me}});
            dict.push_back(json{{"Tag", "TargetCharacterName"},
                                {"Text", target->data.value("Name", std::string{})},
                                {"MemberNumber", target->member_number()}});
            room_remove_locked(target, "ServerBan", dict);
            sync_room_properties(room, me);
        } else if (action == "Kick") {
            if (target->socket) target->socket->emit("ChatRoomSearchResponse", "RoomKicked");
            dict.push_back(json{{"Tag", "SourceCharacter"},
                                {"Text", acc->data.value("Name", std::string{})}, {"MemberNumber", me}});
            dict.push_back(json{{"Tag", "TargetCharacterName"},
                                {"Text", target->data.value("Name", std::string{})},
                                {"MemberNumber", target->member_number()}});
            room_remove_locked(target, "ServerKick", dict);
        } else if (action == "MoveLeft" && a != 0) {
            std::swap(members[a], members[a - 1]);
            tgt_dict();
            if (data.value("Publish", false))
                chat_room_message(room, me, "ServerMoveLeft", "Action", json(nullptr), dict);
            sync_reorder_players(room, me);
        } else if (action == "MoveRight" && a + 1 < members.size()) {
            std::swap(members[a], members[a + 1]);
            tgt_dict();
            if (data.value("Publish", false))
                chat_room_message(room, me, "ServerMoveRight", "Action", json(nullptr), dict);
            sync_reorder_players(room, me);
        } else if (action == "Shuffle") {
            static thread_local std::mt19937_64 rng{std::random_device{}()};
            std::shuffle(members.begin(), members.end(), rng);
            chat_room_message(room, me, "ServerShuffle", "Action", json(nullptr), source_dict(*acc));
            sync_reorder_players(room, me);
        } else if (action == "Promote" &&
                   !has_member(room.data.value("Admin", json::array()), target->member_number())) {
            room.data["Admin"].push_back(target->member_number());
            tgt_dict();
            chat_room_message(room, me, "ServerPromoteAdmin", "Action", json(nullptr), dict);
            sync_room_properties(room, me);
        } else if (action == "Demote" &&
                   has_member(room.data.value("Admin", json::array()), target->member_number())) {
            auto& admin = room.data["Admin"];
            admin.erase(std::remove(admin.begin(), admin.end(), json(target->member_number())),
                        admin.end());
            tgt_dict();
            chat_room_message(room, me, "ServerDemoteAdmin", "Action", json(nullptr), dict);
            sync_room_properties(room, me);
        } else if (action == "Whitelist" &&
                   !has_member(room.data.value("Whitelist", json::array()), target_member)) {
            room.data["Whitelist"].push_back(target_member);
            tgt_dict();
            chat_room_message(room, me, "ServerRoomWhitelist", "Action", json(nullptr), dict);
            sync_room_properties(room, me);
        } else if (action == "Unwhitelist" &&
                   has_member(room.data.value("Whitelist", json::array()), target->member_number())) {
            auto& wl = room.data["Whitelist"];
            wl.erase(std::remove(wl.begin(), wl.end(), json(target->member_number())), wl.end());
            tgt_dict();
            chat_room_message(room, me, "ServerRoomUnwhitelist", "Action", json(nullptr), dict);
            sync_room_properties(room, me);
        }
        return;
    }

    // Ban/Unban/Whitelist/Unwhitelist for a member not present in the room.
    auto contains = [&](const char* key, std::int64_t n) {
        return has_member(room.data.value(key, json::array()), n);
    };
    if (action == "Ban" && !contains("Ban", target_member)) {
        room.data["Ban"].push_back(target_member);
        sync_room_properties(room, me);
    } else if (action == "Unban" && contains("Ban", target_member)) {
        auto& b = room.data["Ban"];
        b.erase(std::remove(b.begin(), b.end(), json(target_member)), b.end());
        sync_room_properties(room, me);
    } else if (action == "Whitelist" && !contains("Whitelist", target_member)) {
        room.data["Whitelist"].push_back(target_member);
        sync_room_properties(room, me);
    } else if (action == "Unwhitelist" && contains("Whitelist", target_member)) {
        auto& wl = room.data["Whitelist"];
        wl.erase(std::remove(wl.begin(), wl.end(), json(target_member)), wl.end());
        sync_room_properties(room, me);
    }
}

void ChatRoomManager::chat_room_allow_item(std::shared_ptr<socketio::Socket> socket, json data) {
    if (!data.is_object() || !data.contains("MemberNumber") || !data["MemberNumber"].is_number_integer())
        return;
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto acc = account_for(socket->id());
    if (!acc || !acc->chat_room) return;
    std::int64_t target = data["MemberNumber"].get<std::int64_t>();
    for (const auto& room_acc : acc->chat_room->accounts) {
        if (room_acc->member_number() == target) {
            socket->emit("ChatRoomAllowItem",
                         json{{"MemberNumber", target}, {"AllowItem", get_allow_item(*acc, *room_acc)}});
            return;
        }
    }
}

}  // namespace sbc::server::gameserver
