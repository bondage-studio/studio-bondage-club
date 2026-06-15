#include "server/rpc/session.hpp"

#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "server/rpc/dispatcher.hpp"

namespace sbc::server::rpc {

namespace asio = boost::asio;
using json = nlohmann::ordered_json;

RpcSession::RpcSession(asio::any_io_executor exec, const RpcDispatcher& dispatcher, Sender send)
    : exec_(std::move(exec)), dispatcher_(dispatcher), send_(std::move(send)) {}

void RpcSession::handle_frame(const json& msg) {
    if (stopped_ || !msg.is_object()) return;
    const std::string t = msg.value("t", std::string{});

    if (t == "req") {
        auto id = msg.value("id", std::int64_t{0});
        std::string method = msg.value("method", std::string{});
        json params = msg.contains("params") ? msg["params"] : json::object();
        auto self = shared_from_this();
        // Run each request concurrently so a slow handler (blocking-pool work)
        // does not stall other in-flight calls. The client correlates responses
        // by id, so out-of-order completion is fine.
        asio::co_spawn(
            exec_,
            [self, id, method = std::move(method),
             params = std::move(params)]() -> asio::awaitable<void> {
                json res;
                res["t"] = "res";
                res["id"] = id;
                try {
                    json result = co_await self->dispatcher_.call(method, params);
                    res["ok"] = true;
                    res["result"] = std::move(result);
                } catch (const RpcError& e) {
                    res["ok"] = false;
                    res["error"] = {{"code", e.code}, {"message", e.what()}};
                } catch (const std::exception& e) {
                    res["ok"] = false;
                    res["error"] = {{"code", "internal"}, {"message", e.what()}};
                }
                self->send_(res.dump());
                co_return;
            },
            asio::detached);
    } else if (t == "sub") {
        start_subscription(msg.value("id", std::int64_t{0}), msg.value("method", std::string{}));
    } else if (t == "unsub") {
        stop_subscription(msg.value("id", std::int64_t{0}));
    }
}

void RpcSession::start_subscription(std::int64_t id, const std::string& method) {
    const RpcDispatcher::Stream* stream = dispatcher_.find_stream(method);
    if (stream == nullptr) {
        send_(json{
            {"t", "res"},
            {"id", id},
            {"ok", false},
            {"error", {{"code", "method_not_found"}, {"message", "unknown stream: " + method}}}}
                  .dump());
        return;
    }
    auto timer = std::make_shared<asio::steady_timer>(exec_);
    subs_[id] = timer;
    auto self = shared_from_this();
    auto produce = stream->produce;
    auto interval = stream->interval;
    asio::co_spawn(
        exec_,
        [self, id, produce, interval, timer]() -> asio::awaitable<void> {
            try {
                for (;;) {
                    if (self->subs_.find(id) == self->subs_.end() || self->stopped_) co_return;
                    json data = co_await produce();
                    if (self->subs_.find(id) == self->subs_.end() || self->stopped_) co_return;
                    self->send_(json{{"t", "event"}, {"id", id}, {"data", std::move(data)}}.dump());
                    timer->expires_after(interval);
                    boost::system::error_code ec;
                    co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    if (ec || self->stopped_) co_return;  // cancelled or shutting down
                }
            } catch (...) {
                // Producer failure: drop this stream silently rather than terminate.
            }
        },
        asio::detached);
}

void RpcSession::stop_subscription(std::int64_t id) {
    auto it = subs_.find(id);
    if (it == subs_.end()) return;
    it->second->cancel();
    subs_.erase(it);
}

void RpcSession::cancel_all() {
    stopped_ = true;
    for (auto& [id, timer] : subs_) timer->cancel();
    subs_.clear();
}

}  // namespace sbc::server::rpc