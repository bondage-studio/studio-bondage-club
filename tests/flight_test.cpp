#include <atomic>
#include <chrono>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "cache/flight.hpp"
#include "test_framework.hpp"

namespace asio = boost::asio;
using namespace sbc;

SBC_TEST(flight_coalesces_concurrent_calls) {
    asio::io_context ioc;
    cache::FlightGroup<int> fg;
    std::atomic<int> calls{0};
    std::vector<int> results(5, 0);

    auto worker = [&](int idx) -> asio::awaitable<void> {
        int v = co_await fg.do_call("key", [&]() -> asio::awaitable<int> {
            ++calls;
            asio::steady_timer t(co_await asio::this_coro::executor);
            t.expires_after(std::chrono::milliseconds(30));
            co_await t.async_wait(asio::use_awaitable);
            co_return 42;
        });
        results[idx] = v;
    };

    for (int i = 0; i < 5; ++i) asio::co_spawn(ioc, worker(i), asio::detached);
    ioc.run();

    CHECK(calls.load() == 1);
    for (int i = 0; i < 5; ++i) CHECK(results[i] == 42);
}

SBC_TEST(flight_propagates_exceptions) {
    asio::io_context ioc;
    cache::FlightGroup<int> fg;
    int caught = 0;

    auto worker = [&]() -> asio::awaitable<void> {
        try {
            co_await fg.do_call("k", [&]() -> asio::awaitable<int> {
                throw std::runtime_error("boom");
                co_return 0;
            });
        } catch (const std::exception&) {
            ++caught;
        }
    };

    asio::co_spawn(ioc, worker(), asio::detached);
    ioc.run();
    CHECK(caught == 1);
}
