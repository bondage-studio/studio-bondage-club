#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace sbc::cache {

// FlightGroup coalesces concurrent operations on the same key: only the first
// caller (leader) runs the work; others wait and share the result. This is the
// coroutine-aware analog of Go's singleflight/FlightGroup, built on a
// steady_timer used as a one-shot async event (no thread blocking).
template <typename T>
class FlightGroup {
public:
    // do_call runs fn() for the first caller of `key` and shares its result with
    // any concurrent callers. fn must return awaitable<T>. Exceptions thrown by
    // fn propagate to every waiter.
    boost::asio::awaitable<T> do_call(std::string key,
                                      std::function<boost::asio::awaitable<T>()> fn) {
        namespace asio = boost::asio;
        auto executor = co_await asio::this_coro::executor;

        std::shared_ptr<Call> call;
        bool leader = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = calls_.find(key);
            if (it != calls_.end()) {
                call = it->second;
            } else {
                call = std::make_shared<Call>(executor);
                calls_[key] = call;
                leader = true;
            }
        }

        if (!leader) {
            boost::system::error_code ec;
            co_await call->timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            if (call->error) std::rethrow_exception(call->error);
            co_return *call->value;
        }

        // Leader: run the work, then publish + wake waiters. The completion is
        // guaranteed even on exception via the publish block below.
        std::optional<T> value;
        std::exception_ptr error;
        try {
            value = co_await fn();
        } catch (...) {
            error = std::current_exception();
        }

        call->value = std::move(value);
        call->error = error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            calls_.erase(key);
        }
        // Setting the expiry to the past wakes any pending waiters AND makes a
        // late waiter's async_wait return immediately, closing the join race.
        call->timer.expires_at(std::chrono::steady_clock::time_point::min());

        if (error) std::rethrow_exception(error);
        co_return *call->value;
    }

private:
    struct Call {
        explicit Call(boost::asio::any_io_executor ex) : timer(std::move(ex)) {
            timer.expires_at(std::chrono::steady_clock::time_point::max());
        }
        boost::asio::steady_timer timer;
        std::optional<T> value;
        std::exception_ptr error;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Call>> calls_;
};

}  // namespace sbc::cache
