#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cache/metadata.hpp"

namespace sbc::cache {

// TempFileGuard removes a temp file on destruction. Shared so a kept-temp
// (MISS-UNCACHED) body survives being moved into a flight result and is cleaned
// up once after it has been streamed to the client.
class TempFileGuard {
public:
    explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~TempFileGuard();
    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Writer streams a response body to temporary storage, then either commits it to
// the backend or keeps it as a temp file for one-shot streaming.
class Writer {
public:
    virtual ~Writer() = default;
    virtual void write(std::string_view data) = 0;
    // commit finalizes the body into the store and returns the stored Metadata
    // (with body_size/body_sha256 recomputed from the actual bytes).
    virtual Metadata commit(Metadata meta) = 0;
    // keep_temp stops without committing and returns the temp path plus a guard
    // that deletes it when released.
    virtual std::pair<std::filesystem::path, std::shared_ptr<TempFileGuard>> keep_temp() = 0;
    virtual void abort() = 0;
};

// Backend is the cache storage interface (LevelDbStore implements it). All
// methods are synchronous and invoked from the blocking pool via run_blocking.
class Backend {
public:
    virtual ~Backend() = default;

    virtual std::string name() const = 0;

    // get returns the entry metadata, or nullopt on a miss.
    virtual std::optional<Metadata> get(const std::string& key) = 0;
    // open_body returns the full stored body. Throws if absent.
    virtual std::string open_body(const std::string& key) = 0;
    virtual std::unique_ptr<Writer> new_writer(const std::string& key) = 0;
    // update_metadata atomically read-transform-writes metadata. nullopt on miss.
    virtual std::optional<Metadata> update_metadata(
        const std::string& key, const std::function<Metadata(Metadata)>& fn) = 0;
    virtual void touch(const std::string& key, TimePoint now) = 0;
    virtual void clear() = 0;
    virtual Stats stats() = 0;
    virtual void enforce_max_size(std::int64_t max_bytes) = 0;
};

}  // namespace sbc::cache
