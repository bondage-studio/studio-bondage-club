#include "server/gameserver/game/game_db.hpp"

#include <filesystem>

#include <algorithm>

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>

#include "common/error.hpp"
#include "common/json_merge.hpp"

namespace sbc::server::gameserver {

namespace fs = std::filesystem;

namespace {
std::string name_key(const std::string& upper) {
    return "acc/name/" + upper;
}
std::string member_key(std::int64_t n) {
    return "acc/member/" + std::to_string(n);
}
constexpr const char* kNextMemberKey = "meta/nextMemberNumber";
}  // namespace

std::shared_ptr<GameDb> GameDb::open(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);

    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, dir, &db);
    if (!status.ok()) throw Error("open game db " + dir + ": " + status.ToString());
    return std::shared_ptr<GameDb>(new GameDb(db));
}

GameDb::~GameDb() {
    delete db_;
}

void GameDb::close() {
    delete db_;
    db_ = nullptr;
}

void GameDb::reopen(const std::string& dir) {
    close();
    std::error_code ec;
    fs::create_directories(dir, ec);
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, dir, &db);
    if (!status.ok()) throw Error("reopen game db " + dir + ": " + status.ToString());
    db_ = db;
}

bool GameDb::account_exists(const std::string& upper_name) {
    if (!db_) return false;
    std::string val;
    return db_->Get(leveldb::ReadOptions(), name_key(upper_name), &val).ok();
}

std::optional<nlohmann::json> GameDb::get_account(const std::string& upper_name) {
    if (!db_) return std::nullopt;
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), name_key(upper_name), &val);
    if (!s.ok()) return std::nullopt;
    auto j = nlohmann::json::parse(val, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

std::optional<nlohmann::json> GameDb::get_account_by_member(std::int64_t member_number) {
    if (!db_) return std::nullopt;
    std::string upper;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), member_key(member_number), &upper);
    if (!s.ok()) return std::nullopt;
    return get_account(upper);
}

void GameDb::put_account(const nlohmann::json& account) {
    if (!db_) throw Error("put_account: game db is closed (mid-migration)");
    std::string upper = account.value("AccountName", std::string{});
    if (upper.empty()) throw Error("put_account: missing AccountName");
    leveldb::WriteBatch batch;
    batch.Put(name_key(upper), account.dump());
    if (account.contains("MemberNumber") && account["MemberNumber"].is_number_integer()) {
        batch.Put(member_key(account["MemberNumber"].get<std::int64_t>()), upper);
    }
    leveldb::Status s = db_->Write(leveldb::WriteOptions(), &batch);
    if (!s.ok()) throw Error("put_account " + upper + ": " + s.ToString());
}

void GameDb::update_account_fields(const std::string& upper_name, const nlohmann::json& fields) {
    auto existing = get_account(upper_name);
    if (!existing) return;
    merge_set_fields(*existing, fields);
    put_account(*existing);
}

std::vector<nlohmann::json> GameDb::accounts_by_email(const std::string& email) {
    if (!db_) return {};
    std::string want = email;
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<nlohmann::json> out;
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
    const std::string prefix = "acc/name/";
    for (it->Seek(prefix); it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        if (key.compare(0, prefix.size(), prefix) != 0) break;
        auto j = nlohmann::json::parse(it->value().ToString(), nullptr, false);
        if (j.is_discarded()) continue;
        std::string e = j.value("Email", std::string{});
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!e.empty() && e == want) out.push_back(j);
    }
    return out;
}

std::int64_t GameDb::next_member_number() {
    if (!db_) throw Error("next_member_number: game db is closed (mid-migration)");
    std::string val;
    std::int64_t next = 1;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), kNextMemberKey, &val);
    if (s.ok()) {
        try {
            next = std::stoll(val);
        } catch (...) {
            next = 1;
        }
    }
    std::int64_t assigned = next;
    leveldb::Status w = db_->Put(leveldb::WriteOptions(), kNextMemberKey, std::to_string(next + 1));
    if (!w.ok()) throw Error("next_member_number: " + w.ToString());
    return assigned;
}

}  // namespace sbc::server::gameserver
