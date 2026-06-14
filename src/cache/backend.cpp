#include "cache/backend.hpp"

namespace sbc::cache {

TempFileGuard::~TempFileGuard() {
    if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
}

}  // namespace sbc::cache
