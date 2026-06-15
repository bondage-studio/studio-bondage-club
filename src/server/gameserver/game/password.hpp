#pragma once

#include <string>
#include <string_view>

namespace sbc::server::gameserver {

// hash_password derives a salted PBKDF2-HMAC-SHA256 hash and returns a
// self-describing string: "pbkdf2$<iterations>$<salt_b64>$<hash_b64>".
// `iterations` is configurable; the iteration count is embedded in the output so
// existing hashes keep verifying when it changes.
std::string hash_password(std::string_view password, int iterations = 100000);

// verify_password checks a candidate password against a stored hash produced by
// hash_password. Returns false on any malformed stored value.
bool verify_password(std::string_view password, std::string_view stored);

}  // namespace sbc::server::gameserver
