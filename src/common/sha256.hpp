#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace sbc::crypto {

// sha256_hex returns the lowercase hex SHA-256 digest of the given data. Used
// for cache keys (sha256 of URL or upstream-relative path) and body integrity.
std::string sha256_hex(std::string_view data);

// Sha256 is an incremental hasher so response bodies can be hashed while they
// stream to disk, without buffering the whole body in memory.
class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(std::string_view data);
    void update(const void* data, std::size_t len);

    // hex finalizes and returns the lowercase hex digest. The hasher must not
    // be updated after this call.
    std::string hex();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sbc::crypto
