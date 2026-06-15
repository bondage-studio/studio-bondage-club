#include "common/random.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <openssl/rand.h>
#endif

namespace sbc::crypto {

std::string random_bytes(std::size_t n) {
    std::string out(n, '\0');
    if (n == 0) return out;
#ifdef _WIN32
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()),
                                        static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        throw std::runtime_error("random_bytes: BCryptGenRandom failed");
    }
#else
    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(n)) != 1) {
        throw std::runtime_error("random_bytes: RAND_bytes failed");
    }
#endif
    return out;
}

std::string random_hex(std::size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string raw = random_bytes(n);
    std::string out(n * 2, '\0');
    for (std::size_t i = 0; i < n; ++i) {
        auto b = static_cast<unsigned char>(raw[i]);
        out[2 * i] = kHex[b >> 4];
        out[2 * i + 1] = kHex[b & 0x0F];
    }
    return out;
}

}  // namespace sbc::crypto
