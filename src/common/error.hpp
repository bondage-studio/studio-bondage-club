#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace sbc {

// Error is the base type for all recoverable, message-carrying failures. The
// message is surfaced to the React panel verbatim, so keep messages stable and
// human-readable.
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

// ValidationError is thrown by config validation; its message is shown in the
// admin panel on a failed PUT /api/config.
class ValidationError : public Error {
public:
    explicit ValidationError(const std::string& message) : Error(message) {}
};

}  // namespace sbc
