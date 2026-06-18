#include "cache/leveldb_store.hpp"

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include "common/error.hpp"
#include "common/sha256.hpp"

#if defined(_WIN32)
#include <process.h>
#define SBC_GETPID _getpid
#else
#include <unistd.h>
#define SBC_GETPID getpid
#endif

namespace sbc::cache {

namespace fs = std::filesystem;

namespace {

std::string meta_key(const std::string& key) {
    return "m/" + key;
}
std::string body_key(const std::string& key) {
    return "b/" + key;
}

std::int64_t to_millis(TimePoint tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}
TimePoint from_millis(std::int64_t ms) {
    return TimePoint(std::chrono::milliseconds(ms));
}

std::string metadata_to_json(const Metadata& meta) {
    nlohmann::json j;
    j["key"] = meta.key;
    j["url"] = meta.url;
    j["statusCode"] = meta.status_code;
    nlohmann::json headers = nlohmann::json::object();
    for (const auto& e : meta.header.entries()) headers[e.first].push_back(e.second);
    j["header"] = headers;
    j["storedAt"] = to_millis(meta.stored_at);
    j["lastAccessedAt"] = to_millis(meta.last_accessed_at);
    j["expiresAt"] = meta.expires_at.has_value() ? to_millis(*meta.expires_at) : 0;
    j["bodySize"] = meta.body_size;
    j["bodySha256"] = meta.body_sha256;
    if (!meta.version.empty()) j["version"] = meta.version;
    return j.dump();
}

Metadata metadata_from_json(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text);
    Metadata meta;
    meta.key = j.value("key", "");
    meta.url = j.value("url", "");
    meta.status_code = j.value("statusCode", 0);
    if (auto it = j.find("header"); it != j.end() && it->is_object()) {
        for (auto el = it->begin(); el != it->end(); ++el) {
            if (el->is_array()) {
                for (const auto& v : *el) meta.header.add(el.key(), v.get<std::string>());
            }
        }
    }
    meta.stored_at = from_millis(j.value("storedAt", std::int64_t{0}));
    meta.last_accessed_at = from_millis(j.value("lastAccessedAt", std::int64_t{0}));
    std::int64_t exp = j.value("expiresAt", std::int64_t{0});
    if (exp != 0) meta.expires_at = from_millis(exp);
    meta.body_size = j.value("bodySize", std::int64_t{0});
    meta.body_sha256 = j.value("bodySha256", "");
    meta.version = j.value("version", "");
    return meta;
}

bool starts_with(const leveldb::Slice& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

std::string read_whole_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("read temp body: cannot open " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// LevelDbWriter streams a body to a temp file, then commits or keeps it.
class LevelDbWriter : public Writer {
public:
    LevelDbWriter(LevelDbStore* store, std::string key, fs::path temp_path)
        : store_(store),
          key_(std::move(key)),
          temp_path_(std::move(temp_path)),
          out_(temp_path_, std::ios::binary | std::ios::trunc) {
        if (!out_) throw Error("create cache temp body: " + temp_path_.string());
    }

    ~LevelDbWriter() override {
        if (!finished_) {
            out_.close();
            std::error_code ec;
            fs::remove(temp_path_, ec);
        }
    }

    void write(std::string_view data) override {
        out_.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out_) throw Error("write cache temp body failed");
    }

    Metadata commit(Metadata meta) override {
        out_.flush();
        out_.close();
        finished_ = true;
        meta.key = key_;
        return store_->commit_temp(temp_path_, std::move(meta));
    }

    std::pair<fs::path, std::shared_ptr<TempFileGuard>> keep_temp() override {
        out_.flush();
        out_.close();
        finished_ = true;
        return {temp_path_, std::make_shared<TempFileGuard>(temp_path_)};
    }

    void abort() override {
        out_.close();
        finished_ = true;
        std::error_code ec;
        fs::remove(temp_path_, ec);
    }

private:
    LevelDbStore* store_;
    std::string key_;
    fs::path temp_path_;
    std::ofstream out_;
    bool finished_ = false;
};

}  // namespace

std::shared_ptr<LevelDbStore> LevelDbStore::open(const std::string& name, const std::string& dir) {
    fs::path db_path = fs::path(dir) / "leveldb";
    std::error_code ec;
    fs::create_directories(db_path, ec);

    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, db_path.string(), &db);
    if (!status.ok()) {
        throw Error("open leveldb store " + db_path.string() + ": " + status.ToString());
    }
    return std::shared_ptr<LevelDbStore>(new LevelDbStore(name, dir, db));
}

LevelDbStore::LevelDbStore(std::string name, std::string dir, leveldb::DB* db)
    : name_(std::move(name)), dir_(std::move(dir)), db_(db) {}

LevelDbStore::~LevelDbStore() = default;

std::optional<Metadata> LevelDbStore::get(const std::string& key) {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), meta_key(key), &val);
    if (s.IsNotFound()) return std::nullopt;
    if (!s.ok()) throw Error("leveldb get meta " + key + ": " + s.ToString());

    // Body and metadata are written in one atomic batch (store_body) and deleted
    // together (enforce_max_size/clear), so a present "m/" implies a present "b/";
    // no separate existence probe is needed.
    return metadata_from_json(val);
}

std::string LevelDbStore::open_body(const std::string& key) {
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), body_key(key), &val);
    if (s.IsNotFound()) throw Error("cache body missing: " + key);
    if (!s.ok()) throw Error("leveldb get body " + key + ": " + s.ToString());
    return val;
}

std::unique_ptr<Writer> LevelDbStore::new_writer(const std::string& key) {
    fs::path dir = temp_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    static std::atomic<std::uint64_t> counter{0};
    std::string name = key + "." + std::to_string(SBC_GETPID()) + "." +
                       std::to_string(counter.fetch_add(1)) + ".tmp";
    return std::make_unique<LevelDbWriter>(this, key, dir / name);
}

Metadata LevelDbStore::store_body(std::string body, Metadata meta) {
    meta.body_sha256 = crypto::sha256_hex(body);
    meta.body_size = static_cast<std::int64_t>(body.size());

    leveldb::WriteBatch batch;
    batch.Put(body_key(meta.key), body);
    batch.Put(meta_key(meta.key), metadata_to_json(meta));
    // sync=false: a cache entry lost on crash just triggers a re-fetch; durability
    // is not worth an fsync per write. LevelDB still persists via its WAL.
    leveldb::WriteOptions wo;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("leveldb store " + meta.key + ": " + s.ToString());

    // Keep the running byte total in sync (only once seeded; otherwise the next
    // enforce_max_size() scan establishes it).
    if (total_bytes_.load(std::memory_order_relaxed) >= 0) {
        total_bytes_.fetch_add(meta.body_size, std::memory_order_relaxed);
    }
    return meta;
}

Metadata LevelDbStore::put(const std::string& key, std::string body, Metadata meta) {
    meta.key = key;
    return store_body(std::move(body), std::move(meta));
}

Metadata LevelDbStore::commit_temp(const fs::path& temp_path, Metadata meta) {
    std::string body = read_whole_file(temp_path);
    std::error_code ec;
    fs::remove(temp_path, ec);
    return store_body(std::move(body), std::move(meta));
}

std::optional<Metadata> LevelDbStore::update_metadata(const std::string& key,
                                                      const std::function<Metadata(Metadata)>& fn) {
    std::lock_guard<std::mutex> lock(write_mu_);
    std::string val;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), meta_key(key), &val);
    if (s.IsNotFound()) return std::nullopt;
    if (!s.ok()) throw Error("leveldb get meta for update " + key + ": " + s.ToString());

    Metadata meta = fn(metadata_from_json(val));
    leveldb::WriteOptions wo;  // sync=false: metadata updates need no fsync
    s = db_->Put(wo, meta_key(key), metadata_to_json(meta));
    if (!s.ok()) throw Error("leveldb set updated meta " + key + ": " + s.ToString());
    return meta;
}

void LevelDbStore::touch(const std::string& key, TimePoint now) {
    try {
        update_metadata(key, [now](Metadata meta) {
            meta.last_accessed_at = now;
            return meta;
        });
    } catch (...) {
        // best-effort; ignore
    }
}

void LevelDbStore::clear() {
    leveldb::WriteBatch batch;
    {
        std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            if (starts_with(it->key(), "m/") || starts_with(it->key(), "b/")) {
                batch.Delete(it->key());
            }
        }
    }
    leveldb::WriteOptions wo;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("leveldb clear: " + s.ToString());
    total_bytes_.store(0, std::memory_order_relaxed);
}

Stats LevelDbStore::stats() {
    Stats stats;
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
    for (it->Seek("m/"); it->Valid() && starts_with(it->key(), "m/"); it->Next()) {
        try {
            Metadata meta = metadata_from_json(it->value().ToString());
            stats.entries++;
            stats.bytes += meta.body_size;
        } catch (...) {
            // skip invalid entry
        }
    }
    return stats;
}

void LevelDbStore::enforce_max_size(std::int64_t max_bytes) {
    if (max_bytes <= 0) return;

    // Fast path: while the running total is under budget, skip the full scan that
    // would otherwise run on every write. -1 means "not yet seeded" -> fall
    // through to the scan below, which both evicts (if needed) and seeds it.
    std::int64_t hint = total_bytes_.load(std::memory_order_relaxed);
    if (hint >= 0 && hint <= max_bytes) return;

    struct Candidate {
        std::string key;
        std::int64_t size;
        TimePoint last_access;
    };
    std::vector<Candidate> candidates;
    std::int64_t total = 0;

    {
        std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
        for (it->Seek("m/"); it->Valid() && starts_with(it->key(), "m/"); it->Next()) {
            Metadata meta;
            try {
                meta = metadata_from_json(it->value().ToString());
            } catch (...) {
                continue;
            }
            TimePoint access = meta.last_accessed_at;
            if (access.time_since_epoch().count() == 0) access = meta.stored_at;
            candidates.push_back({meta.key, meta.body_size, access});
            total += meta.body_size;
        }
    }

    if (total <= max_bytes) {
        total_bytes_.store(total, std::memory_order_relaxed);  // seed/correct the hint
        return;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.last_access < b.last_access; });

    leveldb::WriteBatch batch;
    for (const auto& c : candidates) {
        if (total <= max_bytes) break;
        batch.Delete(meta_key(c.key));
        batch.Delete(body_key(c.key));
        total -= c.size;
    }
    leveldb::WriteOptions wo;
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("leveldb evict: " + s.ToString());
    total_bytes_.store(total, std::memory_order_relaxed);  // exact post-eviction total
}

int LevelDbStore::expire(const std::function<bool(const Metadata&)>& match, TimePoint when) {
    std::lock_guard<std::mutex> lock(write_mu_);
    leveldb::WriteBatch batch;
    int affected = 0;
    {
        std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
        for (it->Seek("m/"); it->Valid() && starts_with(it->key(), "m/"); it->Next()) {
            Metadata meta;
            try {
                meta = metadata_from_json(it->value().ToString());
            } catch (...) {
                continue;
            }
            if (!match(meta)) continue;
            meta.expires_at = when;
            batch.Put(it->key(), metadata_to_json(meta));
            ++affected;
        }
    }
    if (affected == 0) return 0;
    leveldb::WriteOptions wo;  // sync=false: expiry only flips metadata timestamps
    leveldb::Status s = db_->Write(wo, &batch);
    if (!s.ok()) throw Error("leveldb expire: " + s.ToString());
    return affected;
}

std::vector<std::pair<std::string, int>> LevelDbStore::versions() {
    std::map<std::string, int> counts;
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(leveldb::ReadOptions()));
    for (it->Seek("m/"); it->Valid() && starts_with(it->key(), "m/"); it->Next()) {
        try {
            Metadata meta = metadata_from_json(it->value().ToString());
            if (!meta.version.empty()) counts[meta.version]++;
        } catch (...) {
            // skip invalid entry
        }
    }
    return {counts.begin(), counts.end()};
}

}  // namespace sbc::cache
