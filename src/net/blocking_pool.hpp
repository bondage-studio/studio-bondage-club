#pragma once

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstddef>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace sbc::net {

// BlockingPool offloads synchronous, potentially-blocking work (LevelDB calls,
// temp-file I/O) off the Asio I/O threads, mirroring Go's goroutine-per-syscall
// model. Coroutines bridge onto it via run_blocking().
class BlockingPool {
public:
    explicit BlockingPool(std::size_t threads = 4) : pool_(threads) {}

    boost::asio::thread_pool& pool() { return pool_; }

    void stop() { pool_.stop(); }
    void join() { pool_.join(); }

private:
    boost::asio::thread_pool pool_;
};

// run_blocking posts f() to the blocking pool, awaits its completion, and
// resumes the calling coroutine on its original executor. Exceptions thrown by
// f propagate out of the co_await. The result type R must be default
// constructible (all cache/store result types are).
template <typename F>
boost::asio::awaitable<std::invoke_result_t<F>> run_blocking(BlockingPool& bp, F f) {
    namespace asio = boost::asio;
    using R = std::invoke_result_t<F>;

    if constexpr (std::is_void_v<R>) {
        co_await asio::async_initiate<decltype(asio::use_awaitable), void(std::exception_ptr)>(
            [&bp, f = std::move(f)](auto handler) mutable {
                asio::post(bp.pool(), [f = std::move(f), handler = std::move(handler)]() mutable {
                    std::exception_ptr ep;
                    try {
                        f();
                    } catch (...) {
                        ep = std::current_exception();
                    }
                    auto ex = asio::get_associated_executor(handler);
                    asio::post(ex, [handler = std::move(handler), ep]() mutable {
                        std::move(handler)(ep);
                    });
                });
            },
            asio::use_awaitable);
        co_return;
    } else {
        R result = co_await asio::async_initiate<decltype(asio::use_awaitable),
                                                 void(std::exception_ptr, R)>(
            [&bp, f = std::move(f)](auto handler) mutable {
                asio::post(bp.pool(), [f = std::move(f), handler = std::move(handler)]() mutable {
                    std::exception_ptr ep;
                    std::optional<R> value;
                    try {
                        value.emplace(f());
                    } catch (...) {
                        ep = std::current_exception();
                    }
                    auto ex = asio::get_associated_executor(handler);
                    asio::post(ex, [handler = std::move(handler), ep,
                                    value = std::move(value)]() mutable {
                        if (ep) {
                            std::move(handler)(ep, R{});
                        } else {
                            std::move(handler)(std::exception_ptr{}, std::move(*value));
                        }
                    });
                });
            },
            asio::use_awaitable);
        co_return result;
    }
}

}  // namespace sbc::net
