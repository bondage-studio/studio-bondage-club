#pragma once

#include <functional>
#include <memory>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

#include "config/config.hpp"

namespace sbc::net {
class BlockingPool;
class TlsContext;
}  // namespace sbc::net

namespace sbc::server {

class UserscriptStore;

// UserscriptUpdater periodically checks every enabled, auto-update userscript for
// a newer @version at its updateURL/downloadURL. It NEVER applies an update: a
// newer version is recorded as a pending record (store.set_pending) only, and
// takes effect solely when the frontend confirms via apply-update. The interval
// is read from the store's settings each pass (0 -> idle, re-armed on a long
// fallback). check_now() runs one pass on demand and returns a summary.
class UserscriptUpdater {
public:
    UserscriptUpdater(boost::asio::any_io_executor ex, net::BlockingPool& blocking,
                      net::TlsContext& tls, std::shared_ptr<UserscriptStore> store,
                      std::function<config::Config()> config_fn);

    // start launches the background loop (idempotent). stop cancels it.
    void start();
    void stop();

    // check_now runs a single check pass and returns a summary JSON:
    //   { "checked": N, "updates": [ { id, name, fromVersion, toVersion } ] }
    boost::asio::awaitable<nlohmann::json> check_now();

private:
    boost::asio::awaitable<void> run_loop();
    // run_pass checks all eligible scripts, records pending updates, returns the
    // same summary shape as check_now.
    boost::asio::awaitable<nlohmann::json> run_pass();

    boost::asio::any_io_executor ex_;
    net::BlockingPool& blocking_;
    net::TlsContext& tls_;
    std::shared_ptr<UserscriptStore> store_;
    std::function<config::Config()> config_fn_;
    boost::asio::steady_timer timer_;
    bool started_ = false;
    bool stopped_ = false;
};

}  // namespace sbc::server
