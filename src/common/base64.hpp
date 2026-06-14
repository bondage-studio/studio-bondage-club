#pragma once

#include <string>
#include <string_view>

namespace sbc {

// base64_encode returns the standard Base64 encoding (with '+' '/' and '='
// padding) of the input bytes.
std::string base64_encode(std::string_view data);

// base64_decode decodes standard Base64. Returns the decoded bytes; on malformed
// input it decodes what it can and stops (callers using it for self-produced
// data do not need strict validation).
std::string base64_decode(std::string_view data);

}  // namespace sbc
