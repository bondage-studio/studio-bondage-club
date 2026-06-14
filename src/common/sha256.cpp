#include "common/sha256.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <stdexcept>

namespace sbc::crypto {

namespace {

std::string to_hex(const unsigned char* digest, unsigned int len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(static_cast<std::size_t>(len) * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 0x0F];
    }
    return out;
}

}  // namespace

struct Sha256::Impl {
    EVP_MD_CTX* ctx = nullptr;
};

Sha256::Sha256() : impl_(std::make_unique<Impl>()) {
    impl_->ctx = EVP_MD_CTX_new();
    if (impl_->ctx == nullptr || EVP_DigestInit_ex(impl_->ctx, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("sha256: failed to initialize digest context");
    }
}

Sha256::~Sha256() {
    if (impl_ && impl_->ctx) EVP_MD_CTX_free(impl_->ctx);
}

void Sha256::update(std::string_view data) { update(data.data(), data.size()); }

void Sha256::update(const void* data, std::size_t len) {
    if (len == 0) return;
    if (EVP_DigestUpdate(impl_->ctx, data, len) != 1) {
        throw std::runtime_error("sha256: digest update failed");
    }
}

std::string Sha256::hex() {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(impl_->ctx, digest.data(), &len) != 1) {
        throw std::runtime_error("sha256: digest finalization failed");
    }
    return to_hex(digest.data(), len);
}

std::string sha256_hex(std::string_view data) {
    Sha256 h;
    h.update(data);
    return h.hex();
}

}  // namespace sbc::crypto
