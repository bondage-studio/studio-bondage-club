#pragma once

#include <memory>
#include <mutex>

#include "config/config.hpp"

namespace sbc::server::gameserver {

// GameSettings is a copy-on-write holder for the live GameServerConfig. The game
// managers read a snapshot at the point of use, so a settings change (via
// store()) is picked up by the next event / tick / new hash with zero teardown
// of in-flight connections. Reads return a shared_ptr snapshot that stays valid
// even if store() swaps the pointer concurrently.
class GameSettings {
public:
    GameSettings() : cfg_(std::make_shared<config::GameServerConfig>()) {}
    explicit GameSettings(const config::GameServerConfig& c)
        : cfg_(std::make_shared<config::GameServerConfig>(c)) {}

    std::shared_ptr<const config::GameServerConfig> snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return cfg_;
    }

    void store(const config::GameServerConfig& c) {
        auto next = std::make_shared<config::GameServerConfig>(c);
        std::lock_guard<std::mutex> lock(mu_);
        cfg_ = std::move(next);
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const config::GameServerConfig> cfg_;
};

}  // namespace sbc::server::gameserver
