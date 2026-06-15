#include "server/rpc/auth.hpp"

#include "common/random.hpp"

namespace sbc::server::rpc {

RpcAuth::RpcAuth() : token_(sbc::crypto::random_hex(32)) {}

bool RpcAuth::verify(std::string_view candidate) const {
    // The length is not secret, so an early length check is fine; the byte
    // comparison itself is constant-time over the secret's length.
    if (candidate.size() != token_.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < token_.size(); ++i) {
        diff |= static_cast<unsigned char>(token_[i]) ^ static_cast<unsigned char>(candidate[i]);
    }
    return diff == 0;
}

}  // namespace sbc::server::rpc
