#include "server/userscript_updater.hpp"

#include <chrono>
#include <optional>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include "common/url.hpp"
#include "net/blocking_pool.hpp"
#include "net/http_client.hpp"
#include "net/socks5.hpp"
#include "net/tls.hpp"
#include "server/userscript_metadata.hpp"
#include "server/userscript_store.hpp"

namespace sbc::server {

namespace asio = boost::asio;
using nlohmann::json;

UserscriptUpdater::UserscriptUpdater(asio::any_io_executor ex, net::BlockingPool& blocking,
                                     net::TlsContext& tls, std::shared_ptr<UserscriptStore> store,
                                     std::function<config::Config()> config_fn)
    : ex_(std::move(ex)),
      blocking_(blocking),
      tls_(tls),
      store_(std::move(store)),
      config_fn_(std::move(config_fn)),
      timer_(ex_) {}

void UserscriptUpdater::start() {
    if (started_) return;
    started_ = true;
    asio::co_spawn(ex_, run_loop(), asio::detached);
}

void UserscriptUpdater::stop() {
    stopped_ = true;
    timer_.cancel();
}

asio::awaitable<void> UserscriptUpdater::run_loop() {
    // Initial settle delay so startup isn't competing with the first page load.
    boost::system::error_code ec;
    timer_.expires_after(std::chrono::seconds(15));
    co_await timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));

    while (!stopped_) {
        try {
            co_await run_pass();
        } catch (const std::exception& e) {
            spdlog::warn("userscript update pass failed: {}", e.what());
        }
        if (stopped_) break;

        int hours = 6;
        try {
            json settings = co_await net::run_blocking(
                blocking_, [store = store_]() { return store->get_settings(); });
            hours = settings.value("updateIntervalHours", 6);
        } catch (...) {
        }
        // 0 (or negative) disables periodic checks; re-arm on a long fallback so
        // a later settings change is still picked up without a restart.
        auto delay = std::chrono::hours(hours > 0 ? hours : 24);
        timer_.expires_after(delay);
        co_await timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (ec) break;  // cancelled
    }
}

asio::awaitable<nlohmann::json> UserscriptUpdater::check_now() {
    co_return co_await run_pass();
}

asio::awaitable<nlohmann::json> UserscriptUpdater::run_pass() {
    config::Config cfg = config_fn_();

    std::optional<net::Socks5Config> socks;
    if (auto u = config::parse_socks5_proxy(cfg.socks5_proxy)) {
        socks = net::socks5_config_from_url(*u);
    }
    net::HttpClient client(ex_, tls_, socks);

    std::vector<json> scripts =
        co_await net::run_blocking(blocking_, [store = store_]() { return store->list(); });

    json updates = json::array();
    int checked = 0;
    for (const auto& script : scripts) {
        bool enabled = script.value("enabled", false);
        bool auto_update = script.value("autoUpdate", false);
        if (!enabled || !auto_update) continue;

        std::string url = script.value("downloadURL", "");
        if (url.empty()) url = script.value("updateURL", "");
        if (url.empty()) continue;

        std::string id = script.value("id", "");
        std::string name = script.value("name", "");
        std::string current = script.value("version", "");

        ++checked;
        std::string source;
        try {
            auto target = Url::try_parse(url);
            if (!target || target->host().empty()) continue;
            net::ClientRequest creq;
            creq.method = "GET";
            creq.target = *target;
            net::StringSink sink;
            net::HeadHandler on_head = [](const net::ClientResponse&) -> asio::awaitable<void> {
                co_return;
            };
            co_await client.fetch(creq, on_head, sink);
            source = std::move(sink.body);
        } catch (const std::exception& e) {
            spdlog::debug("userscript update fetch {} failed: {}", url, e.what());
            continue;
        }
        if (source.empty()) continue;

        std::string new_version = parse_metadata_field(source, "version");
        if (!version_newer(new_version, current)) continue;

        std::int64_t fetched_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        json pending = {{"version", new_version}, {"source", source}, {"fetchedAt", fetched_at}};
        try {
            co_await net::run_blocking(
                blocking_, [store = store_, id, pending]() { store->set_pending(id, pending); });
        } catch (const std::exception& e) {
            spdlog::warn("userscript set_pending {} failed: {}", id, e.what());
            continue;
        }
        updates.push_back(
            {{"id", id}, {"name", name}, {"fromVersion", current}, {"toVersion", new_version}});
    }

    co_return json{{"checked", checked}, {"updates", updates}};
}

}  // namespace sbc::server
