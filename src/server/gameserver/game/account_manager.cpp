#include "server/gameserver/game/account_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <regex>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include "common/json_merge.hpp"
#include "net/blocking_pool.hpp"
#include "server/gameserver/game/chatroom.hpp"
#include "server/gameserver/game/chatroom_manager.hpp"
#include "server/gameserver/game/game_db.hpp"
#include "server/gameserver/game/mailer.hpp"
#include "server/gameserver/game/password.hpp"
#include "server/gameserver/socketio/server.hpp"
#include "server/gameserver/socketio/socket_facade.hpp"

namespace sbc::server::gameserver {

namespace asio = boost::asio;
using json = nlohmann::json;
using namespace std::chrono;

namespace {

constexpr const char* kEnvironment = "LOCAL";

std::int64_t now_ms() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool matches(const std::string& s, const std::regex& re) { return std::regex_match(s, re); }

std::string trim_copy(const std::string& s) {
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

bool has_str(const nlohmann::json& arr, const char* s) {
    if (!arr.is_array()) return false;
    for (const auto& e : arr)
        if (e.is_string() && e.get<std::string>() == s) return true;
    return false;
}

bool has_member(const nlohmann::json& arr, std::int64_t n) {
    if (!arr.is_array()) return false;
    for (const auto& e : arr)
        if (e.is_number_integer() && e.get<std::int64_t>() == n) return true;
    return false;
}

bool email_ok(const std::string& email, int max_len) {
    if (email.empty()) return true;
    if (static_cast<int>(email.size()) > max_len) return false;
    static const std::regex re(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    return matches(email, re);
}

// AccountValidData: ensure the defaultable fields exist with correct types.
void valid_data(json& a) {
    if (!a.contains("ItemPermission") || !a["ItemPermission"].is_number()) a["ItemPermission"] = 2;
    if (!a.contains("WhiteList") || !a["WhiteList"].is_array()) a["WhiteList"] = json::array();
    if (!a.contains("BlackList") || !a["BlackList"].is_array()) a["BlackList"] = json::array();
    if (!a.contains("FriendList") || !a["FriendList"].is_array()) a["FriendList"] = json::array();
}

// AccountPurgeInfo: drop fields the server does not need to keep in memory.
void purge_info(json& a) {
    for (const char* k : {"Log", "Skill", "Wardrobe", "WardrobeCharacterNames", "ChatSettings",
                          "VisualSettings", "AudioSettings", "GameplaySettings", "Email", "Password",
                          "LastLogin", "GhostList", "HiddenItems"}) {
        a.erase(k);
    }
}

}  // namespace

AccountManager::AccountManager(asio::any_io_executor ex, net::BlockingPool& blocking, GameDb& db,
                               socketio::SocketIoServer& hub, Mailer& mailer, GameState& state,
                               const GameSettings& settings)
    : ex_(std::move(ex)),
      blocking_(blocking),
      db_(db),
      hub_(hub),
      mailer_(mailer),
      gs_(state),
      settings_(settings),
      server_info_timer_(ex_),
      delayed_timer_(ex_) {
    (void)mailer_;  // used by PasswordReset (Phase 8)
}

void AccountManager::start() {
    asio::co_spawn(ex_, server_info_loop(), asio::detached);
    asio::co_spawn(ex_, delayed_update_loop(), asio::detached);
}

void AccountManager::close() {
    stopped_ = true;
    server_info_timer_.cancel();
    delayed_timer_.cancel();
    flush_delayed_updates();
}

std::shared_ptr<OnlineAccount> AccountManager::get_by_socket(const std::string& id) {
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto it = gs_.by_socket.find(id);
    return it == gs_.by_socket.end() ? nullptr : it->second;
}

std::shared_ptr<OnlineAccount> AccountManager::get_by_member(std::int64_t member_number) {
    std::lock_guard<std::mutex> lock(gs_.mu);
    auto it = gs_.by_member.find(member_number);
    return it == gs_.by_member.end() ? nullptr : it->second;
}

std::size_t AccountManager::online_count() {
    std::lock_guard<std::mutex> lock(gs_.mu);
    return gs_.by_socket.size();
}

void AccountManager::on_socket_connected(std::shared_ptr<socketio::Socket> socket) {
    register_pre_login(socket);
    std::string id = socket->id();
    socket->once_disconnect([this, id](const std::string&) { remove_account(id); });
    send_server_info(socket);
}

void AccountManager::register_pre_login(std::shared_ptr<socketio::Socket> socket) {
    socket->on("AccountCreate", [this, socket](const json& data) {
        asio::co_spawn(ex_, account_create(socket, data), asio::detached);
    });
    socket->on("AccountLogin", [this, socket](const json& data) { enqueue_login(socket, data); });
    socket->on("PasswordReset", [this, socket](const json& data) {
        if (data.is_string())
            asio::co_spawn(ex_, password_reset(socket, data.get<std::string>()), asio::detached);
    });
    socket->on("PasswordResetProcess", [this, socket](const json& data) {
        asio::co_spawn(ex_, password_reset_process(socket, data), asio::detached);
    });
}

asio::awaitable<void> AccountManager::account_create(std::shared_ptr<socketio::Socket> socket,
                                                     json data) {
    auto invalid = [&]() { socket->emit("CreationResponse", "Invalid account information"); };
    if (!data.is_object() || !data.contains("Name") || !data.contains("AccountName") ||
        !data.contains("Password") || !data.contains("Email") || !data["Name"].is_string() ||
        !data["AccountName"].is_string() || !data["Password"].is_string() ||
        !data["Email"].is_string()) {
        invalid();
        co_return;
    }
    std::string name = data["Name"];
    std::string account_name = data["AccountName"];
    std::string password = data["Password"];
    std::string email = data["Email"];

    auto cfg = settings_.snapshot();
    const std::string len = std::to_string(cfg->name_max_len);
    const std::regex name_re("^[a-zA-Z ]{1," + len + "}$");
    const std::regex acct_re("^[a-zA-Z0-9]{1," + len + "}$");
    if (!matches(name, name_re) || !matches(account_name, acct_re) ||
        !matches(password, acct_re) || !email_ok(email, cfg->email_max_len)) {
        co_return;  // matches app.js: invalid format is silently ignored
    }

    // Per-IP account-creation rate limit (12/day total, 4/hour by default), as in app.js.
    const int kMaxPerDay = cfg->account_create_per_day;
    const int kMaxPerHour = cfg->account_create_per_hour;
    std::string address = socket->address();
    if (!address.empty()) {
        std::int64_t now = now_ms();
        int total = 0, hour = 0;
        {
            std::lock_guard<std::mutex> lock(creation_mu_);
            for (const auto& [addr, t] : account_creation_ip_)
                if (addr == address) {
                    ++total;
                    if (t >= now - 3600000) ++hour;
                }
            if (total >= kMaxPerDay || hour >= kMaxPerHour) {
                socket->emit("CreationResponse", "New accounts per day exceeded");
                co_return;
            }
            account_creation_ip_.emplace_back(address, now);
        }
    }

    std::string upper = to_upper(account_name);
    bool exists = co_await net::run_blocking(blocking_, [this, upper]() { return db_.account_exists(upper); });
    if (exists) {
        socket->emit("CreationResponse", "Account already exists");
        co_return;
    }

    std::string pw_upper = to_upper(password);
    int iterations = cfg->pbkdf2_iterations;
    std::string hash = co_await net::run_blocking(
        blocking_, [pw_upper, iterations]() { return hash_password(pw_upper, iterations); });
    std::int64_t member =
        co_await net::run_blocking(blocking_, [this]() { return db_.next_member_number(); });

    json account;
    account["AccountName"] = upper;
    account["Email"] = email;
    account["Password"] = hash;
    account["MemberNumber"] = member;
    account["Name"] = name;
    account["Money"] = 100;
    account["Creation"] = now_ms();
    account["LastLogin"] = now_ms();
    account["Environment"] = kEnvironment;
    account["Lovership"] = json::array();
    account["ItemPermission"] = 2;
    account["FriendList"] = json::array();
    account["WhiteList"] = json::array();
    account["BlackList"] = json::array();

    co_await net::run_blocking(blocking_, [this, account]() { db_.put_account(account); });

    auto acc = std::make_shared<OnlineAccount>();
    acc->id = socket->id();
    acc->socket = socket;
    acc->data = account;
    acc->data["ID"] = acc->id;
    valid_data(acc->data);
    purge_info(acc->data);

    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        gs_.by_socket[acc->id] = acc;
        gs_.by_member[member] = acc;
    }
    spdlog::info("gameserver: account created {} member={} id={}", upper, member, acc->id);
    on_login(socket, acc);
    socket->emit("CreationResponse",
                 json{{"ServerAnswer", "AccountCreated"}, {"OnlineID", acc->id}, {"MemberNumber", member}});
    send_server_info(socket);
}

void AccountManager::enqueue_login(std::shared_ptr<socketio::Socket> socket, json data) {
    if (!data.is_object() || !data.contains("AccountName") || !data["AccountName"].is_string() ||
        !data.contains("Password") || !data["Password"].is_string()) {
        socket->emit("LoginResponse", "InvalidNamePassword");
        return;
    }
    std::string upper = to_upper(data["AccountName"].get<std::string>());
    std::string password = data["Password"];

    bool should_run = false;
    std::size_t queue_len = 0;
    {
        std::lock_guard<std::mutex> lock(login_mu_);
        should_run = !login_running_;
        login_queue_.emplace_back(socket, upper, password);
        queue_len = login_queue_.size();
        if (should_run) login_running_ = true;
    }
    if (static_cast<int>(queue_len) > settings_.snapshot()->login_queue_threshold)
        socket->emit("LoginQueue", static_cast<std::int64_t>(queue_len));
    if (should_run) asio::co_spawn(ex_, run_login_queue(), asio::detached);
}

asio::awaitable<void> AccountManager::run_login_queue() {
    for (;;) {
        std::shared_ptr<socketio::Socket> socket;
        std::string upper, password;
        {
            std::lock_guard<std::mutex> lock(login_mu_);
            if (login_queue_.empty()) {
                login_running_ = false;
                co_return;
            }
            std::tie(socket, upper, password) = login_queue_.front();
            login_queue_.pop_front();
        }
        // Skip clients that disconnected while waiting.
        if (!socket->id().empty()) {
            co_await account_login_process(socket, upper, password);
        }
        bool more;
        {
            std::lock_guard<std::mutex> lock(login_mu_);
            more = !login_queue_.empty();
            if (!more) login_running_ = false;
        }
        if (!more) co_return;
        // Login pacing between logins (50ms by default), mirroring the original server.
        asio::steady_timer pacer(ex_, milliseconds(settings_.snapshot()->login_pace_ms));
        boost::system::error_code ec;
        co_await pacer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }
}

asio::awaitable<void> AccountManager::account_login_process(
    std::shared_ptr<socketio::Socket> socket, std::string upper_name, std::string password) {
    struct LoadResult {
        bool found = false;
        bool ok = false;
        json account;
    };
    std::string pw_upper = to_upper(password);
    auto res = co_await net::run_blocking(blocking_, [this, upper_name, pw_upper]() {
        LoadResult r;
        auto a = db_.get_account(upper_name);
        if (!a) return r;
        r.found = true;
        r.account = *a;
        r.ok = verify_password(pw_upper, a->value("Password", std::string{}));
        return r;
    });

    if (socket->id().empty()) co_return;  // disconnected during DB load
    if (!res.found || !res.ok) {
        socket->emit("LoginResponse", "InvalidNamePassword");
        co_return;
    }

    json& account = res.account;

    // Force-disconnect any existing session for this account name.
    std::shared_ptr<socketio::Socket> dup_socket;
    std::string dup_id;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        for (auto& [id, acc] : gs_.by_socket) {
            if (acc->account_name() == upper_name) {
                dup_socket = acc->socket;
                dup_id = id;
                break;
            }
        }
    }
    if (dup_socket) {
        dup_socket->emit("ForceDisconnect", "ErrorDuplicatedLogin");
        dup_socket->disconnect();
        remove_account(dup_id);
    }

    // Assign a member number if missing (defensive; created accounts always have one).
    bool assigned_member = false;
    if (!account.contains("MemberNumber") || !account["MemberNumber"].is_number_integer()) {
        std::int64_t member =
            co_await net::run_blocking(blocking_, [this]() { return db_.next_member_number(); });
        account["MemberNumber"] = member;
        assigned_member = true;
    }

    // Lovership must be an array.
    if (!account.contains("Lovership") || !account["Lovership"].is_array()) {
        if (account.contains("Lovership") && !account["Lovership"].is_null()) {
            account["Lovership"] = json::array({account["Lovership"]});
        } else {
            account["Lovership"] = json::array();
        }
    }

    account["LastLogin"] = now_ms();
    account["ID"] = socket->id();
    account["Environment"] = kEnvironment;
    valid_data(account);

    // Persist LastLogin (+ member number if just assigned).
    std::string upper = upper_name;
    json persist = {{"LastLogin", account["LastLogin"]}};
    if (assigned_member) persist["MemberNumber"] = account["MemberNumber"];
    co_await net::run_blocking(blocking_,
                               [this, upper, persist]() { db_.update_account_fields(upper, persist); });

    // LoginResponse carries the full account minus the secret fields.
    json response = account;
    response.erase("Password");
    response.erase("Email");

    auto acc = std::make_shared<OnlineAccount>();
    acc->id = socket->id();
    acc->socket = socket;
    acc->data = account;
    purge_info(acc->data);
    std::int64_t member = acc->member_number();
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        gs_.by_socket[acc->id] = acc;
        gs_.by_member[member] = acc;
    }
    spdlog::info("gameserver: login {} member={} id={}", upper_name, member, acc->id);
    on_login(socket, acc);
    socket->emit("LoginResponse", response);
    send_server_info(socket);
}

void AccountManager::on_login(std::shared_ptr<socketio::Socket> socket,
                              std::shared_ptr<OnlineAccount> acc) {
    // Replace the pre-login handlers with the post-login set.
    socket->off("AccountCreate");
    socket->off("AccountLogin");
    std::string id = acc->id;
    socket->on("AccountUpdate", [this, id](const json& data) { account_update(id, data); });
    socket->on("AccountQuery",
               [this, socket](const json& data) { account_query(socket, data); });
    socket->on("AccountBeep", [this, id](const json& data) { account_beep(id, data); });
    socket->on("AccountOwnership", [this, id](const json& data) { account_ownership(id, data); });
    socket->on("AccountLovership", [this, id](const json& data) { account_lovership(id, data); });
    socket->on("AccountDifficulty", [this, id](const json& data) { account_difficulty(id, data); });
    socket->on("AccountUpdateEmail",
               [this, socket](const json& data) { account_update_email(socket, data); });
    socket->on("AccountDisconnect", [this, id](const json&) { remove_account(id); });
    socket->off("PasswordReset");
    socket->off("PasswordResetProcess");

    if (post_login_hook_) post_login_hook_(socket, acc);
}

void AccountManager::account_update(const std::string& socket_id, json data) {
    if (!data.is_object()) return;

    static const std::array<const char*, 22> immutable = {
        "Name",        "AccountName",  "Password",  "Email",
        "Creation",    "LastLogin",    "Pose",      "ActivePose",
        "ChatRoom",    "ID",           "Socket",    "Inventory",
        "_id",         "MemberNumber", "Environment", "Ownership",
        "Lovership",   "Difficulty",   "AssetFamily", "DelayedAppearanceUpdate",
        "DelayedSkillUpdate", "DelayedGameUpdate"};

    std::string upper;
    json to_persist;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        auto it = gs_.by_socket.find(socket_id);
        if (it == gs_.by_socket.end()) return;
        auto& acc = it->second;
        upper = acc->account_name();

        for (const char* k : immutable) data.erase(k);

        // Merge remaining fields into the in-memory account. Dotted keys (e.g.
        // "ExtensionSettings.BCX") are expanded into nested paths to mirror the
        // original MongoDB-backed server, keeping the in-memory copy consistent
        // with what gets persisted. See common/json_merge.hpp.
        merge_set_fields(acc->data, data);

        // Delayed single-field updates are kept in memory, not written immediately.
        if (data.size() == 1 && data.contains("Appearance")) {
            acc->delayed_appearance = data["Appearance"];
            return;
        }
        if (data.size() == 1 && data.contains("Skill")) {
            acc->delayed_skill = data["Skill"];
            return;
        }
        if (data.size() == 1 && data.contains("Game")) {
            acc->delayed_game = data["Game"];
            return;
        }
        // A multi-field update supersedes any pending delayed write.
        if (data.size() > 1) {
            if (data.contains("Appearance")) acc->delayed_appearance.reset();
            if (data.contains("Skill")) acc->delayed_skill.reset();
            if (data.contains("Game")) acc->delayed_game.reset();
        }

        data.erase("MapData");  // the map is never persisted
        if (data.empty()) return;
        to_persist = std::move(data);
    }

    asio::co_spawn(
        ex_,
        [this, upper, to_persist]() -> asio::awaitable<void> {
            co_await net::run_blocking(
                blocking_, [this, upper, to_persist]() { db_.update_account_fields(upper, to_persist); });
        },
        asio::detached);
}

void AccountManager::account_query(std::shared_ptr<socketio::Socket> socket, json data) {
    if (!data.is_object() || !data.contains("Query") || !data["Query"].is_string()) return;
    std::string query = data["Query"];

    if (query == "OnlineFriends") {
        std::string id = socket->id();
        json friends = json::array();
        {
            std::lock_guard<std::mutex> lock(gs_.mu);
            auto self_it = gs_.by_socket.find(id);
            if (self_it == gs_.by_socket.end()) return;
            auto& self = self_it->second;
            std::int64_t my_member = self->member_number();
            std::string my_env = self->environment();
            json my_friend_list =
                self->data.contains("FriendList") ? self->data["FriendList"] : json::array();

            auto friend_info = [](const char* type, const OnlineAccount& a) {
                json fi;
                fi["Type"] = type;
                fi["MemberNumber"] = a.member_number();
                fi["MemberName"] = a.data.value("Name", std::string{});
                if (a.data.contains("Nickname")) fi["MemberNickname"] = a.data["Nickname"];
                return fi;
            };

            std::vector<std::int64_t> indexed;
            for (auto& [oid, other] : gs_.by_socket) {
                if (other->environment() != my_env) continue;
                bool is_owned = other->data.contains("Ownership") &&
                                other->data["Ownership"].is_object() &&
                                other->data["Ownership"].value("MemberNumber", std::int64_t{-1}) ==
                                    my_member;
                bool is_lover = false;
                if (other->data.contains("Lovership") && other->data["Lovership"].is_array()) {
                    for (const auto& l : other->data["Lovership"])
                        if (l.is_object() && l.value("MemberNumber", std::int64_t{-1}) == my_member)
                            is_lover = true;
                }
                if (is_owned || is_lover) {
                    friends.push_back(friend_info(is_owned ? "Submissive" : "Lover", *other));
                    indexed.push_back(other->member_number());
                }
            }
            for (const auto& f : my_friend_list) {
                if (!f.is_number_integer()) continue;
                std::int64_t fn = f.get<std::int64_t>();
                if (std::find(indexed.begin(), indexed.end(), fn) != indexed.end()) continue;
                for (auto& [oid, other] : gs_.by_socket) {
                    if (other->member_number() != fn) continue;
                    if (other->environment() == my_env && other->data.contains("FriendList") &&
                        other->data["FriendList"].is_array()) {
                        const auto& ofl = other->data["FriendList"];
                        if (std::find(ofl.begin(), ofl.end(), json(my_member)) != ofl.end())
                            friends.push_back(friend_info("Friend", *other));
                    }
                    break;
                }
            }
        }
        socket->emit("AccountQueryResult", json{{"Query", query}, {"Result", friends}});
        return;
    }

    if (query == "EmailStatus") {
        asio::co_spawn(ex_, account_query_email_status(socket), asio::detached);
        return;
    }
}

asio::awaitable<void> AccountManager::account_query_email_status(
    std::shared_ptr<socketio::Socket> socket) {
    std::string id = socket->id();
    auto acc = get_by_socket(id);
    if (!acc) co_return;
    std::string upper = acc->account_name();
    bool has_email = co_await net::run_blocking(blocking_, [this, upper]() {
        auto a = db_.get_account(upper);
        return a && !a->value("Email", std::string{}).empty();
    });
    socket->emit("AccountQueryResult", json{{"Query", "EmailStatus"}, {"Result", has_email}});
}

void AccountManager::persist_fields(const std::string& upper_name, json fields) {
    asio::co_spawn(
        ex_,
        [this, upper_name, fields]() -> asio::awaitable<void> {
            co_await net::run_blocking(
                blocking_, [this, upper_name, fields]() { db_.update_account_fields(upper_name, fields); });
        },
        asio::detached);
}

void AccountManager::persist_member_fields(std::int64_t member_number, json fields) {
    asio::co_spawn(
        ex_,
        [this, member_number, fields]() -> asio::awaitable<void> {
            co_await net::run_blocking(blocking_, [this, member_number, fields]() {
                auto a = db_.get_account_by_member(member_number);
                if (a) db_.update_account_fields(a->value("AccountName", std::string{}), fields);
            });
        },
        asio::detached);
}

namespace {
json source_char_dict(const OnlineAccount& acc) {
    return json::array({json{{"Tag", "SourceCharacter"},
                             {"Text", acc.data.value("Name", std::string{})},
                             {"MemberNumber", acc.member_number()}}});
}
}  // namespace

void AccountManager::account_ownership(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("MemberNumber") || !data["MemberNumber"].is_number_integer())
        return;
    std::int64_t target_member = data["MemberNumber"].get<std::int64_t>();
    std::string action =
        (data.contains("Action") && data["Action"].is_string()) ? data["Action"].get<std::string>() : "";
    const std::int64_t kDelay = settings_.snapshot()->relationship_delay_ms;  // 7 days by default

    std::lock_guard<std::mutex> lock(gs_.mu);
    auto sit = gs_.by_socket.find(socket_id);
    if (sit == gs_.by_socket.end()) return;
    auto acc = sit->second;
    std::int64_t my = acc->member_number();

    // The submissive can flush its owner (trial any time, or after the delay if collared;
    // Extreme-difficulty players cannot break a full ownership).
    if (acc->data.contains("Ownership") && acc->data["Ownership"].is_object()) {
        auto& o = acc->data["Ownership"];
        if (o.contains("Stage") && o["Stage"].is_number_integer() && o.contains("Start") &&
            o["Start"].is_number_integer()) {
            std::int64_t stage = o["Stage"].get<std::int64_t>();
            std::int64_t start = o["Start"].get<std::int64_t>();
            if ((stage == 0 || start + kDelay <= now_ms()) && action == "Break") {
                bool diff_level = acc->data.contains("Difficulty") && acc->data["Difficulty"].is_object() &&
                                  acc->data["Difficulty"].contains("Level") &&
                                  acc->data["Difficulty"]["Level"].is_number_integer();
                std::int64_t level = diff_level ? acc->data["Difficulty"]["Level"].get<std::int64_t>() : 0;
                if (!diff_level || level <= 2 || stage == 0) {
                    acc->data["Owner"] = "";
                    acc->data["Ownership"] = json(nullptr);
                    persist_fields(acc->account_name(), json{{"Ownership", json(nullptr)}, {"Owner", ""}});
                    acc->socket->emit("AccountOwnership", json{{"ClearOwnership", true}});
                    return;
                }
            }
        }
    }

    if (!acc->chat_room) return;
    ChatRoom& room = *acc->chat_room;
    std::shared_ptr<OnlineAccount> target;
    for (auto& a : room.accounts)
        if (a->member_number() == target_member) {
            target = a;
            break;
        }

    // Release a target who is not in the room (looked up from the DB).
    if (!target && action == "Release") {
        std::int64_t room_id_holder = my;  // capture acting member; room messages target the actor
        (void)room_id_holder;
        ChatRoom* room_ptr = acc->chat_room;
        asio::co_spawn(
            ex_,
            [this, target_member, my, room_ptr, acc]() -> asio::awaitable<void> {
                auto result = co_await net::run_blocking(
                    blocking_, [this, target_member]() { return db_.get_account_by_member(target_member); });
                std::lock_guard<std::mutex> lk(gs_.mu);
                // Ensure the acting account is still in this room.
                if (acc->chat_room != room_ptr) co_return;
                ChatRoom& rm = *room_ptr;
                bool owned_by_me = result && result->contains("Ownership") &&
                                   (*result)["Ownership"].is_object() &&
                                   (*result)["Ownership"].value("MemberNumber", std::int64_t{-1}) == my;
                if (owned_by_me) {
                    persist_member_fields(target_member, json{{"Owner", ""}, {"Ownership", json(nullptr)}});
                    if (chatrooms_)
                        chatrooms_->broadcast_message(rm, my, "ReleaseSuccess", "ServerMessage", json(my),
                                                      json(nullptr));
                    auto tit = gs_.by_member.find(target_member);
                    if (tit != gs_.by_member.end()) {
                        auto tgt = tit->second;
                        tgt->data["Owner"] = "";
                        tgt->data["Ownership"] = json(nullptr);
                        if (tgt->socket) tgt->socket->emit("AccountOwnership", json{{"ClearOwnership", true}});
                        if (tgt->chat_room && chatrooms_) {
                            chatrooms_->sync_character(*tgt->chat_room, tgt->member_number(),
                                                       tgt->member_number());
                            chatrooms_->broadcast_message(*tgt->chat_room, tgt->member_number(),
                                                          "ReleaseByOwner", "ServerMessage",
                                                          json(tgt->member_number()), json(nullptr));
                        }
                    }
                } else if (chatrooms_) {
                    chatrooms_->broadcast_message(rm, my, "ReleaseFail", "ServerMessage", json(my),
                                                  json(nullptr));
                }
                co_return;
            },
            asio::detached);
        return;
    }

    if (!target) return;

    auto target_black = [&]() { return target->data.value("BlackList", json::array()); };

    // Dominant updates public notes on their fully-owned submissive.
    if (action == "UpdateNotes" && target->data.contains("Ownership") &&
        target->data["Ownership"].is_object() &&
        target->data["Ownership"].value("Stage", std::int64_t{-1}) == 1 &&
        target->data["Ownership"].value("MemberNumber", std::int64_t{-1}) == my) {
        if (data.contains("Notes") && data["Notes"].is_string() && !data["Notes"].get<std::string>().empty())
            target->data["Ownership"]["Notes"] = data["Notes"].get<std::string>().substr(
                0, static_cast<std::size_t>(settings_.snapshot()->ownership_notes_max_len));
        else
            target->data["Ownership"].erase("Notes");
        json o = {{"Ownership", target->data["Ownership"]}, {"Owner", target->data.value("Owner", "")}};
        persist_fields(target->account_name(), o);
        if (target->socket) target->socket->emit("AccountOwnership", o);
        if (chatrooms_) chatrooms_->sync_character(room, target->member_number(), target->member_number());
        return;
    }

    // Dominant releases the submissive at any time.
    if (action == "Release" && target->data.contains("Ownership") &&
        target->data["Ownership"].is_object() &&
        target->data["Ownership"].value("MemberNumber", std::int64_t{-1}) == my) {
        bool is_trial = !(target->data["Ownership"].contains("Stage") &&
                          target->data["Ownership"]["Stage"].is_number_integer()) ||
                        target->data["Ownership"]["Stage"].get<std::int64_t>() == 0;
        target->data["Owner"] = "";
        target->data["Ownership"] = json(nullptr);
        persist_fields(target->account_name(), json{{"Ownership", json(nullptr)}, {"Owner", ""}});
        if (target->socket) target->socket->emit("AccountOwnership", json{{"ClearOwnership", true}});
        json dict = json::array(
            {json{{"Tag", "SourceCharacter"}, {"Text", acc->data.value("Name", std::string{})}, {"MemberNumber", my}},
             json{{"Tag", "TargetCharacter"}, {"Text", target->data.value("Name", std::string{})},
                  {"MemberNumber", target->member_number()}}});
        if (chatrooms_)
            chatrooms_->broadcast_message(room, my, is_trial ? "EndOwnershipTrial" : "EndOwnership",
                                          "ServerMessage", json(nullptr), dict);
        if (chatrooms_) chatrooms_->sync_character(room, target->member_number(), target->member_number());
        return;
    }

    bool my_own_is_target = acc->data.contains("Ownership") && acc->data["Ownership"].is_object() &&
                            acc->data["Ownership"].value("MemberNumber", std::int64_t{-2}) == target_member;

    // Dominant proposes (cannot if already owner of target, on blacklist, or NPC-owned).
    if (!my_own_is_target) {
        if (!has_member(target_black(), my)) {
            std::string target_owner = target->data.value("Owner", std::string{});
            if (target_owner.empty()) {
                bool target_no_owner = !target->data.contains("Ownership") ||
                                       !target->data["Ownership"].is_object() ||
                                       !target->data["Ownership"].contains("MemberNumber");
                // Step 1/4: propose to start trial.
                if (target_no_owner) {
                    if (my != target_member) {
                        if (action == "Propose") {
                            target->data["Owner"] = "";
                            target->data["Ownership"] = json{{"StartTrialOfferedByMemberNumber", my}};
                            if (chatrooms_)
                                chatrooms_->broadcast_message(room, my, "OfferStartTrial", "ServerMessage",
                                                              json(target_member), source_char_dict(*acc));
                        } else {
                            acc->socket->emit("AccountOwnership",
                                              json{{"MemberNumber", target_member}, {"Result", "CanOfferStartTrial"}});
                        }
                    }
                }
                // Step 3/4: dominant offers to end the trial after the delay.
                if (target->data.contains("Ownership") && target->data["Ownership"].is_object() &&
                    target->data["Ownership"].value("MemberNumber", std::int64_t{-1}) == my &&
                    !target->data["Ownership"].contains("EndTrialOfferedByMemberNumber") &&
                    target->data["Ownership"].value("Stage", std::int64_t{-1}) == 0 &&
                    target->data["Ownership"].contains("Start") &&
                    target->data["Ownership"]["Start"].is_number_integer() &&
                    target->data["Ownership"]["Start"].get<std::int64_t>() + kDelay <= now_ms()) {
                    if (action == "Propose") {
                        target->data["Ownership"]["EndTrialOfferedByMemberNumber"] = my;
                        if (chatrooms_)
                            chatrooms_->broadcast_message(room, my, "OfferEndTrial", "ServerMessage",
                                                          json(nullptr), source_char_dict(*acc));
                    } else {
                        acc->socket->emit("AccountOwnership",
                                          json{{"MemberNumber", target_member}, {"Result", "CanOfferEndTrial"}});
                    }
                }
            }
        }
    }

    // Submissive accepts a proposal (no interaction if owned by someone else).
    if (acc->data.contains("Ownership") && acc->data["Ownership"].is_object()) {
        auto& mo = acc->data["Ownership"];
        bool free_or_targets = !mo.contains("MemberNumber") ||
                               mo.value("MemberNumber", std::int64_t{-2}) == target_member;
        if (free_or_targets && !has_member(target_black(), my)) {
            // Step 2/4: accept to start the trial.
            if (mo.value("StartTrialOfferedByMemberNumber", std::int64_t{-1}) == target_member) {
                if (action == "Accept") {
                    acc->data["Owner"] = "";
                    acc->data["Ownership"] = json{{"MemberNumber", target_member},
                                                  {"Name", target->data.value("Name", std::string{})},
                                                  {"Start", now_ms()},
                                                  {"Stage", 0}};
                    persist_fields(acc->account_name(),
                                   json{{"Ownership", acc->data["Ownership"]}, {"Owner", ""}});
                    acc->socket->emit("AccountOwnership",
                                      json{{"Ownership", acc->data["Ownership"]}, {"Owner", ""}});
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "StartTrial", "ServerMessage", json(nullptr),
                                                      source_char_dict(*acc));
                    if (chatrooms_) chatrooms_->sync_character(room, my, my);
                } else {
                    acc->socket->emit("AccountOwnership",
                                      json{{"MemberNumber", target_member}, {"Result", "CanStartTrial"}});
                }
            }
            // Step 4/4: accept the full collar.
            if (acc->data["Ownership"].is_object() &&
                acc->data["Ownership"].value("Stage", std::int64_t{-1}) == 0 &&
                acc->data["Ownership"].value("EndTrialOfferedByMemberNumber", std::int64_t{-1}) == target_member) {
                if (action == "Accept") {
                    acc->data["Owner"] = target->data.value("Name", std::string{});
                    acc->data["Ownership"] = json{{"MemberNumber", target_member},
                                                  {"Name", target->data.value("Name", std::string{})},
                                                  {"Start", now_ms()},
                                                  {"Stage", 1}};
                    persist_fields(acc->account_name(),
                                   json{{"Ownership", acc->data["Ownership"]}, {"Owner", acc->data["Owner"]}});
                    acc->socket->emit("AccountOwnership",
                                      json{{"Ownership", acc->data["Ownership"]}, {"Owner", acc->data["Owner"]}});
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "EndTrial", "ServerMessage", json(nullptr),
                                                      source_char_dict(*acc));
                    if (chatrooms_) chatrooms_->sync_character(room, my, my);
                } else {
                    acc->socket->emit("AccountOwnership",
                                      json{{"MemberNumber", target_member}, {"Result", "CanEndTrial"}});
                }
            }
        }
    }
}

void AccountManager::account_lovership(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("MemberNumber") || !data["MemberNumber"].is_number_integer())
        return;
    std::int64_t target_member = data["MemberNumber"].get<std::int64_t>();
    std::string action =
        (data.contains("Action") && data["Action"].is_string()) ? data["Action"].get<std::string>() : "";
    const std::int64_t kDelay = settings_.snapshot()->relationship_delay_ms;  // 7 days by default

    // Strips offer/dating bookkeeping then persists the lovership by member number.
    auto update_lovership = [this](json lovership, std::int64_t member,
                                   std::shared_ptr<socketio::Socket> sock, bool emit) {
        if (!lovership.is_array()) lovership = json::array();
        for (int L = static_cast<int>(lovership.size()) - 1; L >= 0; --L) {
            lovership[L].erase("BeginEngagementOfferedByMemberNumber");
            lovership[L].erase("BeginWeddingOfferedByMemberNumber");
            if (lovership[L].contains("BeginDatingOfferedByMemberNumber")) lovership.erase(L);
        }
        persist_member_fields(member, json{{"Lovership", lovership}});
        if (emit && sock) sock->emit("AccountLovership", json{{"Lovership", lovership}});
        return lovership;
    };

    std::lock_guard<std::mutex> lock(gs_.mu);
    auto sit = gs_.by_socket.find(socket_id);
    if (sit == gs_.by_socket.end()) return;
    auto acc = sit->second;
    std::int64_t my = acc->member_number();
    if (!acc->data.contains("Lovership") || !acc->data["Lovership"].is_array())
        acc->data["Lovership"] = json::array();

    // --- Break ---
    if (action == "Break") {
        json& lov = acc->data["Lovership"];
        int al = -1;
        for (int i = 0; i < static_cast<int>(lov.size()); ++i) {
            const auto& l = lov[i];
            if (l.contains("MemberNumber") && l["MemberNumber"].is_number_integer()) {
                if (l["MemberNumber"].get<std::int64_t>() == target_member) { al = i; break; }
            } else if (l.contains("Name") && l["Name"].is_string() && data.contains("Name") &&
                       data["Name"].is_string() && l["Name"] == data["Name"]) {
                al = i;
                break;
            }
        }
        if (al >= 0 && lov[al].contains("Stage") && lov[al]["Stage"].is_number_integer() &&
            lov[al].contains("Start") && lov[al]["Start"].is_number_integer()) {
            std::int64_t stage = lov[al]["Stage"].get<std::int64_t>();
            std::int64_t start = lov[al]["Start"].get<std::int64_t>();
            if (stage != 2 || start + kDelay <= now_ms()) {
                // Breaking with another player (online or offline).
                asio::co_spawn(
                    ex_,
                    [this, target_member, my, acc, update_lovership]() -> asio::awaitable<void> {
                        auto result = co_await net::run_blocking(blocking_, [this, target_member]() {
                            return db_.get_account_by_member(target_member);
                        });
                        std::lock_guard<std::mutex> lk(gs_.mu);
                        if (result && result->contains("Lovership") && (*result)["Lovership"].is_array()) {
                            json p = (*result)["Lovership"];
                            int tl = -1;
                            for (int i = 0; i < static_cast<int>(p.size()); ++i)
                                if (p[i].value("MemberNumber", std::int64_t{-1}) == my) { tl = i; break; }
                            if (tl >= 0) {
                                p.erase(tl);
                                auto oit = gs_.by_member.find(target_member);
                                if (oit != gs_.by_member.end()) {
                                    auto other = oit->second;
                                    other->data["Lovership"] = p;
                                    if (other->socket)
                                        other->socket->emit("AccountLovership", json{{"Lovership", p}});
                                    if (other->chat_room && chatrooms_)
                                        chatrooms_->sync_character(*other->chat_room, other->member_number(),
                                                                   other->member_number());
                                }
                                update_lovership(p, target_member, nullptr, false);
                            }
                        }
                        if (target_member == my) co_return;
                        json& lv = acc->data["Lovership"];
                        if (!lv.is_array()) {
                            acc->data["Lovership"] = json::array();
                        } else {
                            for (int i = static_cast<int>(lv.size()) - 1; i >= 0; --i)
                                if (lv[i].value("MemberNumber", std::int64_t{-1}) == target_member) {
                                    lv.erase(i);
                                    break;
                                }
                        }
                        acc->data["Lovership"] =
                            update_lovership(acc->data["Lovership"], my, acc->socket, true);
                        co_return;
                    },
                    asio::detached);
                return;
            }
        } else if (target_member < 0 && data.contains("Name") && data["Name"].is_string()) {
            // Breaking with an NPC.
            if (al >= 0) lov.erase(al);
            acc->data["Lovership"] = update_lovership(lov, my, acc->socket, true);
            return;
        }
    }

    // --- In-room proposal / acceptance flow (6 steps) ---
    if (!acc->chat_room) return;
    ChatRoom& room = *acc->chat_room;

    auto lover_index = [](const json& lovership, std::int64_t member) {
        for (int i = 0; i < static_cast<int>(lovership.size()); ++i) {
            const auto& l = lovership[i];
            std::int64_t key = l.contains("MemberNumber") && l["MemberNumber"].is_number_integer()
                                   ? l["MemberNumber"].get<std::int64_t>()
                                   : (l.contains("BeginDatingOfferedByMemberNumber")
                                          ? l["BeginDatingOfferedByMemberNumber"].get<std::int64_t>()
                                          : -1);
            if (key == member) return i;
        }
        return -1;
    };

    json& my_lov = acc->data["Lovership"];
    int al = lover_index(my_lov, target_member);

    // Proposal side.
    if ((static_cast<int>(my_lov.size()) < 5 && al < 0) || al >= 0) {
        for (auto& room_acc : room.accounts) {
            if (room_acc->member_number() != target_member) continue;
            if (has_member(room_acc->data.value("BlackList", json::array()), my)) break;
            if (!room_acc->data.contains("Lovership") || !room_acc->data["Lovership"].is_array())
                room_acc->data["Lovership"] = json::array();
            json& t_lov = room_acc->data["Lovership"];
            int tl = lover_index(t_lov, my);
            if (my == room_acc->member_number()) return;

            // Step 1/6: propose to start dating.
            if (static_cast<int>(t_lov.size()) < 5 && tl < 0) {
                if (action == "Propose") {
                    t_lov.push_back(json{{"BeginDatingOfferedByMemberNumber", my}});
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "OfferBeginDating", "ServerMessage",
                                                      json(target_member), source_char_dict(*acc));
                } else {
                    acc->socket->emit("AccountLovership",
                                      json{{"MemberNumber", target_member}, {"Result", "CanOfferBeginDating"}});
                }
            }
            // Step 3/6: propose engagement after the delay.
            if (tl >= 0 && !t_lov[tl].contains("BeginEngagementOfferedByMemberNumber") &&
                t_lov[tl].value("Stage", std::int64_t{-1}) == 0 && t_lov[tl].contains("Start") &&
                t_lov[tl]["Start"].is_number_integer() &&
                t_lov[tl]["Start"].get<std::int64_t>() + kDelay <= now_ms()) {
                if (action == "Propose") {
                    t_lov[tl]["BeginEngagementOfferedByMemberNumber"] = my;
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "OfferBeginEngagement", "ServerMessage",
                                                      json(target_member), source_char_dict(*acc));
                } else {
                    acc->socket->emit("AccountLovership", json{{"MemberNumber", target_member},
                                                              {"Result", "CanOfferBeginEngagement"}});
                }
            }
            // Step 5/6: propose wedding after the delay.
            if (tl >= 0 && !t_lov[tl].contains("BeginWeddingOfferedByMemberNumber") &&
                t_lov[tl].value("Stage", std::int64_t{-1}) == 1 && t_lov[tl].contains("Start") &&
                t_lov[tl]["Start"].is_number_integer() &&
                t_lov[tl]["Start"].get<std::int64_t>() + kDelay <= now_ms()) {
                if (action == "Propose") {
                    t_lov[tl]["BeginWeddingOfferedByMemberNumber"] = my;
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "OfferBeginWedding", "ServerMessage",
                                                      json(target_member), source_char_dict(*acc));
                } else {
                    acc->socket->emit("AccountLovership",
                                      json{{"MemberNumber", target_member}, {"Result", "CanOfferBeginWedding"}});
                }
            }
            break;
        }
    }

    // Acceptance side.
    al = lover_index(my_lov, target_member);
    if (static_cast<int>(my_lov.size()) <= 5 && al >= 0) {
        for (auto& room_acc : room.accounts) {
            if (room_acc->member_number() != target_member) continue;
            if (has_member(room_acc->data.value("BlackList", json::array()), my)) break;
            if (!room_acc->data.contains("Lovership") || !room_acc->data["Lovership"].is_array())
                room_acc->data["Lovership"] = json::array();
            json& t_lov = room_acc->data["Lovership"];
            int tl = lover_index(t_lov, my);
            std::string my_name = acc->data.value("Name", std::string{});
            std::string t_name = room_acc->data.value("Name", std::string{});
            auto dict = [&]() {
                return json::array(
                    {json{{"Tag", "SourceCharacter"}, {"Text", my_name}, {"MemberNumber", my}},
                     json{{"Tag", "TargetCharacter"}, {"Text", t_name}, {"MemberNumber", target_member}}});
            };

            // Step 2/6: accept to start dating.
            if (my_lov[al].value("BeginDatingOfferedByMemberNumber", std::int64_t{-1}) == target_member &&
                (static_cast<int>(t_lov.size()) < 5 || tl >= 0)) {
                if (action == "Accept") {
                    my_lov[al] = json{{"MemberNumber", target_member}, {"Name", t_name}, {"Start", now_ms()}, {"Stage", 0}};
                    json entry = {{"MemberNumber", my}, {"Name", my_name}, {"Start", now_ms()}, {"Stage", 0}};
                    if (tl >= 0) t_lov[tl] = entry; else t_lov.push_back(entry);
                    acc->data["Lovership"] = update_lovership(my_lov, my, acc->socket, true);
                    room_acc->data["Lovership"] = update_lovership(t_lov, target_member, room_acc->socket, true);
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "BeginDating", "ServerMessage", json(nullptr), dict());
                    if (chatrooms_) {
                        chatrooms_->sync_character(room, my, my);
                        chatrooms_->sync_character(room, my, target_member);
                    }
                } else {
                    acc->socket->emit("AccountLovership", json{{"MemberNumber", target_member}, {"Result", "CanBeginDating"}});
                }
            }
            // Step 4/6: accept engagement.
            else if (my_lov[al].value("Stage", std::int64_t{-1}) == 0 &&
                     my_lov[al].value("BeginEngagementOfferedByMemberNumber", std::int64_t{-1}) == target_member) {
                if (action == "Accept") {
                    my_lov[al] = json{{"MemberNumber", target_member}, {"Name", t_name}, {"Start", now_ms()}, {"Stage", 1}};
                    if (tl >= 0) t_lov[tl] = json{{"MemberNumber", my}, {"Name", my_name}, {"Start", now_ms()}, {"Stage", 1}};
                    acc->data["Lovership"] = update_lovership(my_lov, my, acc->socket, true);
                    room_acc->data["Lovership"] = update_lovership(t_lov, target_member, room_acc->socket, true);
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "BeginEngagement", "ServerMessage", json(nullptr), dict());
                    if (chatrooms_) {
                        chatrooms_->sync_character(room, my, my);
                        chatrooms_->sync_character(room, my, target_member);
                    }
                } else {
                    acc->socket->emit("AccountLovership", json{{"MemberNumber", target_member}, {"Result", "CanBeginEngagement"}});
                }
            }
            // Step 6/6: accept wedding.
            else if (my_lov[al].value("Stage", std::int64_t{-1}) == 1 &&
                     my_lov[al].value("BeginWeddingOfferedByMemberNumber", std::int64_t{-1}) == target_member) {
                if (action == "Accept") {
                    my_lov[al] = json{{"MemberNumber", target_member}, {"Name", t_name}, {"Start", now_ms()}, {"Stage", 2}};
                    if (tl >= 0) t_lov[tl] = json{{"MemberNumber", my}, {"Name", my_name}, {"Start", now_ms()}, {"Stage", 2}};
                    acc->data["Lovership"] = update_lovership(my_lov, my, acc->socket, true);
                    room_acc->data["Lovership"] = update_lovership(t_lov, target_member, room_acc->socket, true);
                    if (chatrooms_)
                        chatrooms_->broadcast_message(room, my, "BeginWedding", "ServerMessage", json(nullptr), dict());
                    if (chatrooms_) {
                        chatrooms_->sync_character(room, my, my);
                        chatrooms_->sync_character(room, my, target_member);
                    }
                } else {
                    acc->socket->emit("AccountLovership", json{{"MemberNumber", target_member}, {"Result", "CanBeginWedding"}});
                }
            }
            break;
        }
    }
}

void AccountManager::account_beep(const std::string& socket_id, json data) {
    if (!data.is_object() || !data.contains("MemberNumber") || !data["MemberNumber"].is_number_integer())
        return;
    std::int64_t target = data["MemberNumber"].get<std::int64_t>();
    std::string beep_type = (data.contains("BeepType") && data["BeepType"].is_string())
                                ? data["BeepType"].get<std::string>()
                                : "";
    bool is_secret = data.value("IsSecret", false);
    std::shared_ptr<socketio::Socket> target_socket;
    json beep;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        auto acc = gs_.by_socket.find(socket_id);
        if (acc == gs_.by_socket.end()) return;
        auto& me = acc->second;
        auto it = gs_.by_member.find(target);
        if (it == gs_.by_member.end()) return;
        auto& other = it->second;
        if (other->environment() != me->environment()) return;
        // Friend, owner of the target, or a Leash beep.
        bool other_friends_me =
            has_member(other->data.value("FriendList", json::array()), me->member_number());
        json oown = other->data.value("Ownership", json{});
        bool i_own_other = oown.is_object() && oown.value("MemberNumber", std::int64_t{-1}) ==
                                                   me->member_number();
        if (!other_friends_me && !i_own_other && beep_type != "Leash") return;

        beep["MemberNumber"] = me->member_number();
        beep["MemberName"] = me->data.value("Name", std::string{});
        bool hide_room = !me->chat_room || is_secret;
        beep["ChatRoomSpace"] = hide_room ? json(nullptr) : me->chat_room->data.value("Space", json(nullptr));
        beep["ChatRoomName"] = hide_room ? json(nullptr) : me->chat_room->data.value("Name", json(nullptr));
        beep["Private"] = hide_room ? json(nullptr)
                                    : json(!has_str(me->chat_room->data.value("Visibility", json::array()),
                                                    "All"));
        beep["BeepType"] = beep_type.empty() ? json(nullptr) : json(beep_type);
        if (data.contains("Message")) beep["Message"] = data["Message"];
        target_socket = other->socket;
    }
    if (target_socket) target_socket->emit("AccountBeep", beep);
}

void AccountManager::account_difficulty(const std::string& socket_id, json data) {
    if (!data.is_number_integer()) return;
    std::int64_t level = data.get<std::int64_t>();
    if (level < 0 || level > 3) return;
    std::string upper;
    json persist;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        auto it = gs_.by_socket.find(socket_id);
        if (it == gs_.by_socket.end()) return;
        auto& acc = it->second;
        // Levels 2 and 3 require a week since the last change (or account creation).
        std::int64_t last_change = acc->data.value("Creation", now_ms());
        if (acc->data.contains("Difficulty") && acc->data["Difficulty"].is_object() &&
            acc->data["Difficulty"].contains("LastChange") &&
            acc->data["Difficulty"]["LastChange"].is_number_integer())
            last_change = acc->data["Difficulty"]["LastChange"].get<std::int64_t>();
        const std::int64_t kDifficultyDelay =
            settings_.snapshot()->relationship_delay_ms;  // 7 days by default
        if (level > 1 && last_change + kDifficultyDelay >= now_ms()) return;
        json diff = {{"Level", level}, {"LastChange", now_ms()}};
        acc->data["Difficulty"] = diff;
        upper = acc->account_name();
        persist = {{"Difficulty", diff}};
    }
    asio::co_spawn(
        ex_,
        [this, upper, persist]() -> asio::awaitable<void> {
            co_await net::run_blocking(
                blocking_, [this, upper, persist]() { db_.update_account_fields(upper, persist); });
        },
        asio::detached);
}

void AccountManager::account_update_email(std::shared_ptr<socketio::Socket> socket, json data) {
    auto fail = [&]() {
        socket->emit("AccountQueryResult", json{{"Query", "EmailUpdate"}, {"Result", false}});
    };
    if (!data.is_object() || !data.contains("EmailOld") || !data["EmailOld"].is_string() ||
        !data.contains("EmailNew") || !data["EmailNew"].is_string()) {
        fail();
        return;
    }
    std::string email_old = data["EmailOld"];
    std::string email_new = data["EmailNew"];
    if (!email_ok(email_new, settings_.snapshot()->email_max_len)) {  // permits "" (removing email)
        fail();
        return;
    }
    std::string upper;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        auto it = gs_.by_socket.find(socket->id());
        if (it == gs_.by_socket.end()) {
            fail();
            return;
        }
        upper = it->second->account_name();
    }
    asio::co_spawn(
        ex_,
        [this, socket, upper, email_old, email_new]() -> asio::awaitable<void> {
            auto current = co_await net::run_blocking(
                blocking_, [this, upper]() { return db_.get_account(upper); });
            std::string existing = current ? current->value("Email", std::string{}) : "";
            auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            };
            if (!existing.empty() && lower(trim_copy(email_old)) != lower(trim_copy(existing))) {
                socket->emit("AccountQueryResult", json{{"Query", "EmailUpdate"}, {"Result", false}});
                co_return;
            }
            co_await net::run_blocking(blocking_, [this, upper, email_new]() {
                db_.update_account_fields(upper, json{{"Email", email_new}});
            });
            socket->emit("AccountQueryResult", json{{"Query", "EmailUpdate"}, {"Result", true}});
        },
        asio::detached);
}

asio::awaitable<void> AccountManager::password_reset(std::shared_ptr<socketio::Socket> socket,
                                                     std::string email) {
    if (email.empty() || !email_ok(email, settings_.snapshot()->email_max_len)) co_return;
    {
        std::lock_guard<std::mutex> lock(reset_mu_);
        if (next_password_reset_ > now_ms()) {
            socket->emit("PasswordResetResponse", "RetryLater");
            co_return;
        }
        next_password_reset_ = now_ms() + settings_.snapshot()->password_reset_throttle_ms;
    }
    auto accounts = co_await net::run_blocking(
        blocking_, [this, email]() { return db_.accounts_by_email(email); });
    if (accounts.empty()) {
        socket->emit("PasswordResetResponse", "NoAccountOnEmail");
        co_return;
    }
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::int64_t> dist(0, 999999999999LL);
    std::string body =
        "To reset your account password, enter your account name and the reset number.\n\n";
    {
        std::lock_guard<std::mutex> lock(reset_mu_);
        for (const auto& a : accounts) {
            std::string name = a.value("AccountName", std::string{});
            std::string code = std::to_string(dist(rng));
            password_resets_[name] = code;
            body += "Account Name: " + name + "\nReset Number: " + code + "\n\n";
        }
    }
    mailer_.send(Mail{email, "Bondage Club Password Reset", body});
    socket->emit("PasswordResetResponse", "EmailSent");
}

asio::awaitable<void> AccountManager::password_reset_process(std::shared_ptr<socketio::Socket> socket,
                                                             json data) {
    auto invalid = [&]() { socket->emit("PasswordResetResponse", "InvalidPasswordResetInfo"); };
    if (!data.is_object() || !data.contains("AccountName") || !data["AccountName"].is_string() ||
        !data.contains("ResetNumber") || !data["ResetNumber"].is_string() ||
        !data.contains("NewPassword") || !data["NewPassword"].is_string()) {
        invalid();
        co_return;
    }
    std::string account_name = data["AccountName"];
    std::string reset_number = data["ResetNumber"];
    std::string new_password = data["NewPassword"];
    auto cfg = settings_.snapshot();
    const std::regex acct_re("^[a-zA-Z0-9]{1," + std::to_string(cfg->name_max_len) + "}$");
    if (!matches(account_name, acct_re) || !matches(new_password, acct_re)) {
        invalid();
        co_return;
    }
    {
        std::lock_guard<std::mutex> lock(reset_mu_);
        auto it = password_resets_.find(account_name);
        if (it == password_resets_.end() || it->second != reset_number) {
            invalid();
            co_return;
        }
    }
    std::string upper = to_upper(account_name);
    std::string pw_upper = to_upper(new_password);
    int iterations = cfg->pbkdf2_iterations;
    std::string hash = co_await net::run_blocking(
        blocking_, [pw_upper, iterations]() { return hash_password(pw_upper, iterations); });
    co_await net::run_blocking(blocking_, [this, upper, hash]() {
        db_.update_account_fields(upper, json{{"Password", hash}});
    });
    socket->emit("PasswordResetResponse", "PasswordResetSuccessful");
}

void AccountManager::remove_account(const std::string& socket_id) {
    std::shared_ptr<OnlineAccount> acc;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        auto it = gs_.by_socket.find(socket_id);
        if (it == gs_.by_socket.end()) return;
        acc = it->second;
    }
    // Remove from any chat room first (the hook locks GameState itself).
    if (disconnect_hook_) disconnect_hook_(acc);
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        gs_.by_socket.erase(socket_id);
        gs_.by_member.erase(acc->member_number());
    }
    spdlog::info("gameserver: account removed {} id={}", acc->account_name(), socket_id);
    // Flush any delayed updates for this account.
    std::string upper = acc->account_name();
    json persist;
    if (acc->delayed_appearance) persist["Appearance"] = *acc->delayed_appearance;
    if (acc->delayed_skill) persist["Skill"] = *acc->delayed_skill;
    if (acc->delayed_game) persist["Game"] = *acc->delayed_game;
    if (!persist.empty()) {
        asio::co_spawn(
            ex_,
            [this, upper, persist]() -> asio::awaitable<void> {
                co_await net::run_blocking(
                    blocking_, [this, upper, persist]() { db_.update_account_fields(upper, persist); });
            },
            asio::detached);
    }
    // Phase 6: ChatRoomRemove is invoked here.
}

void AccountManager::send_server_info(const std::shared_ptr<socketio::Socket>& socket) {
    json si = {{"Time", now_ms()}, {"OnlinePlayers", static_cast<std::int64_t>(online_count())}};
    if (socket)
        socket->emit("ServerInfo", si);
    else
        hub_.emit_to_all("ServerInfo", si);
}

asio::awaitable<void> AccountManager::server_info_loop() {
    boost::system::error_code ec;
    while (!stopped_) {
        server_info_timer_.expires_after(seconds(settings_.snapshot()->server_info_interval_sec));
        co_await server_info_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (stopped_) break;
        send_server_info(nullptr);
    }
}

asio::awaitable<void> AccountManager::delayed_update_loop() {
    boost::system::error_code ec;
    while (!stopped_) {
        delayed_timer_.expires_after(seconds(settings_.snapshot()->delayed_flush_interval_sec));
        co_await delayed_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (stopped_) break;
        flush_delayed_updates();
    }
}

void AccountManager::flush_delayed_updates() {
    std::vector<std::pair<std::string, json>> work;
    {
        std::lock_guard<std::mutex> lock(gs_.mu);
        for (auto& [id, acc] : gs_.by_socket) {
            json persist;
            if (acc->delayed_appearance) persist["Appearance"] = *acc->delayed_appearance;
            if (acc->delayed_skill) persist["Skill"] = *acc->delayed_skill;
            if (acc->delayed_game) persist["Game"] = *acc->delayed_game;
            acc->delayed_appearance.reset();
            acc->delayed_skill.reset();
            acc->delayed_game.reset();
            if (!persist.empty()) work.emplace_back(acc->account_name(), std::move(persist));
        }
    }
    for (auto& [upper, persist] : work) {
        asio::co_spawn(
            ex_,
            [this, upper, persist]() -> asio::awaitable<void> {
                co_await net::run_blocking(
                    blocking_, [this, upper, persist]() { db_.update_account_fields(upper, persist); });
            },
            asio::detached);
    }
}

}  // namespace sbc::server::gameserver
