#pragma once

#include <cstddef>
#include <string>

namespace sbc::crypto {

// random_bytes returns n cryptographically secure random bytes. Backed by
// OpenSSL RAND_bytes everywhere except Windows, which uses CNG (BCryptGenRandom)
// so no OpenSSL is linked or shipped — matching the SHA-256 backend split.
std::string random_bytes(std::size_t n);

// random_hex returns the lowercase hex encoding of n random bytes (2n chars).
std::string random_hex(std::size_t n);

}  // namespace sbc::crypto
