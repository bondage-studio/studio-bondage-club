#include "cache/key.hpp"

#include "common/sha256.hpp"

namespace sbc::cache {

std::string key(std::string_view raw_url) {
    return crypto::sha256_hex(raw_url);
}

std::string key_from_path(std::string_view real_path) {
    return crypto::sha256_hex(real_path);
}

}  // namespace sbc::cache
