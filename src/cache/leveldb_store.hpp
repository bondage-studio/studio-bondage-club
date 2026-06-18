#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cache/backend.hpp"

namespace leveldb {
class DB;
}

namespace sbc::cache {

// LevelDbStore is the LevelDB-backed cache Backend. Key scheme:
//   "m/<sha256>" -> JSON-encoded Metadata
//   "b/<sha256>" -> raw response body bytes
// Bodies are streamed to a temp file during writes and loaded into LevelDB on
// commit.
class LevelDbStore : public Backend {
public:
    static std::shared_ptr<LevelDbStore> open(const std::string& name, const std::string& dir);
    ~LevelDbStore() override;

    std::string name() const override { return name_; }

    std::optional<Metadata> get(const std::string& key) override;
    std::string open_body(const std::string& key) override;
    std::unique_ptr<Writer> new_writer(const std::string& key) override;
    Metadata put(const std::string& key, std::string body, Metadata meta) override;
    std::optional<Metadata> update_metadata(const std::string& key,
                                            const std::function<Metadata(Metadata)>& fn) override;
    void touch(const std::string& key, TimePoint now) override;
    void clear() override;
    Stats stats() override;
    void enforce_max_size(std::int64_t max_bytes) override;
    int expire(const std::function<bool(const Metadata&)>& match, TimePoint when) override;
    std::vector<std::pair<std::string, int>> versions() override;

    Metadata commit_temp(const std::filesystem::path& temp_path, Metadata meta);

    std::filesystem::path temp_dir() const { return std::filesystem::path(dir_) / "tmp"; }

private:
    LevelDbStore(std::string name, std::string dir, leveldb::DB* db);

    // Shared write path for put()/commit_temp(): compute sha256/body_size, write
    // the body+metadata batch (sync=false) and bump the running byte total.
    Metadata store_body(std::string body, Metadata meta);

    std::string name_;
    std::string dir_;
    std::unique_ptr<leveldb::DB> db_;
    std::mutex write_mu_;

    // Running sum of cached body bytes, so enforce_max_size() is O(1) while under
    // budget instead of a full metadata scan per write. -1 = not yet initialized;
    // the first enforce_max_size() lazily scans to seed it.
    std::atomic<std::int64_t> total_bytes_{-1};
};

}  // namespace sbc::cache
