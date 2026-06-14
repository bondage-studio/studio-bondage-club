#include "server/gameserver/game/password.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include <openssl/evp.h>

#include "common/base64.hpp"

namespace sbc::server::gameserver {

namespace {

constexpr int kSaltLen = 16;
constexpr int kHashLen = 32;  // SHA-256 output

std::string derive(std::string_view password, const std::string& salt, int iterations) {
    std::array<unsigned char, kHashLen> out{};
    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                      reinterpret_cast<const unsigned char*>(salt.data()),
                      static_cast<int>(salt.size()), iterations, EVP_sha256(),
                      static_cast<int>(out.size()), out.data());
    return std::string(reinterpret_cast<char*>(out.data()), out.size());
}

bool constant_time_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t next = s.find(sep, pos);
        out.emplace_back(s.substr(pos, next == std::string_view::npos ? std::string_view::npos
                                                                       : next - pos));
        if (next == std::string_view::npos) break;
        pos = next + 1;
    }
    return out;
}

}  // namespace

std::string hash_password(std::string_view password, int iterations) {
    if (iterations <= 0) iterations = 100000;
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 255);
    std::string salt;
    salt.reserve(kSaltLen);
    for (int i = 0; i < kSaltLen; ++i) salt.push_back(static_cast<char>(dist(rng)));

    std::string hash = derive(password, salt, iterations);
    return "pbkdf2$" + std::to_string(iterations) + "$" + base64_encode(salt) + "$" +
           base64_encode(hash);
}

bool verify_password(std::string_view password, std::string_view stored) {
    auto parts = split(stored, '$');
    if (parts.size() != 4 || parts[0] != "pbkdf2") return false;
    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    if (iterations <= 0) return false;
    std::string salt = base64_decode(parts[2]);
    std::string expected = base64_decode(parts[3]);
    if (salt.empty() || expected.empty()) return false;
    std::string actual = derive(password, salt, iterations);
    return constant_time_equal(actual, expected);
}

}  // namespace sbc::server::gameserver
