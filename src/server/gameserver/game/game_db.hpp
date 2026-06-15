#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace leveldb {
class DB;
}

namespace sbc::server::gameserver {

// GameDb is the embedded game server's persistent account store, backed by its
// own LevelDB instance (separate from the cache stores). It uses raw game keys
// rather than the cache's metadata/body split.
//
// Keys:
//   acc/name/<UPPER_ACCOUNTNAME>  -> full account JSON
//   acc/member/<MemberNumber>     -> UPPER account name (index)
//   meta/nextMemberNumber         -> integer counter
//
// All methods are synchronous and must be invoked off the I/O threads (via
// net::run_blocking).
class GameDb {
public:
    static std::shared_ptr<GameDb> open(const std::string& dir);
    ~GameDb();

    // close releases the LevelDB handle (and its directory lock). reopen opens a
    // fresh handle at `dir`. Together they let the same GameDb object follow a
    // cache-dir migration without invalidating the manager references that hold
    // it. Both must be called with no concurrent DB access in flight.
    void close();
    void reopen(const std::string& dir);

    bool account_exists(const std::string& upper_name);
    std::optional<nlohmann::json> get_account(const std::string& upper_name);
    std::optional<nlohmann::json> get_account_by_member(std::int64_t member_number);

    // put_account writes the full account JSON and refreshes the member-number
    // index. The account JSON must contain "AccountName" (already uppercased) and
    // "MemberNumber".
    void put_account(const nlohmann::json& account);

    // update_fields merges `fields` into the stored account and saves it. No-op if
    // the account does not exist.
    void update_account_fields(const std::string& upper_name, const nlohmann::json& fields);

    std::int64_t next_member_number();

    // accounts_by_email scans all accounts for ones whose Email matches (case
    // insensitive). Used by the password-reset flow (rare; a scan is fine for a
    // local single-user store).
    std::vector<nlohmann::json> accounts_by_email(const std::string& email);

private:
    explicit GameDb(leveldb::DB* db) : db_(db) {}
    leveldb::DB* db_;
};

}  // namespace sbc::server::gameserver
