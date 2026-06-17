#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <nlohmann/json.hpp>

#include "server/rpc/auth.hpp"
#include "server/rpc/dispatcher.hpp"
#include "server/rpc/event_hub.hpp"
#include "test_framework.hpp"

using namespace sbc::server::rpc;
namespace asio = boost::asio;
using nlohmann::ordered_json;

namespace {

// run_sync drives a single awaitable to completion on a private io_context and
// returns its result, so the dispatcher's coroutine methods can be tested
// without a live server.
template <typename T>
T run_sync(asio::awaitable<T> aw) {
    asio::io_context io;
    auto fut = asio::co_spawn(io, std::move(aw), asio::use_future);
    io.run();
    return fut.get();
}

}  // namespace

SBC_TEST(rpc_auth_accepts_only_its_own_token) {
    RpcAuth auth;
    // 32 random bytes -> 64 lowercase hex chars.
    CHECK(auth.token().size() == 64);
    CHECK(auth.verify(auth.token()));
    CHECK(!auth.verify(""));
    CHECK(!auth.verify("deadbeef"));

    // A same-length-but-different token is rejected.
    std::string forged = auth.token();
    forged[0] = forged[0] == 'a' ? 'b' : 'a';
    CHECK(!auth.verify(forged));
}

SBC_TEST(rpc_auth_tokens_differ_per_instance) {
    RpcAuth a;
    RpcAuth b;
    CHECK(a.token() != b.token());
    CHECK(!a.verify(b.token()));
}

SBC_TEST(rpc_dispatcher_round_trips_a_method) {
    RpcDispatcher d;
    d.add("echo", [](const ordered_json& p) -> asio::awaitable<ordered_json> {
        ordered_json out;
        out["got"] = p.value("msg", std::string{});
        co_return out;
    });

    ordered_json params;
    params["msg"] = "hello";
    ordered_json result = run_sync(d.call("echo", params));
    CHECK(result.value("got", std::string{}) == "hello");
}

SBC_TEST(rpc_dispatcher_unknown_method_throws) {
    RpcDispatcher d;
    bool threw = false;
    std::string code;
    try {
        run_sync(d.call("nope", ordered_json::object()));
    } catch (const RpcError& e) {
        threw = true;
        code = e.code;
    }
    CHECK(threw);
    CHECK(code == "method_not_found");
}

SBC_TEST(rpc_dispatcher_propagates_handler_error) {
    RpcDispatcher d;
    d.add("boom", [](const ordered_json&) -> asio::awaitable<ordered_json> {
        throw RpcError("bad_request", "nope");
        co_return ordered_json{};
    });
    std::string code;
    try {
        run_sync(d.call("boom", ordered_json::object()));
    } catch (const RpcError& e) {
        code = e.code;
    }
    CHECK(code == "bad_request");
}

SBC_TEST(rpc_dispatcher_registers_streams) {
    RpcDispatcher d;
    d.add_stream(
        "ticks", []() -> asio::awaitable<ordered_json> { co_return ordered_json{{"n", 1}}; },
        std::chrono::seconds(2));
    CHECK(d.find_stream("ticks") != nullptr);
    CHECK(d.find_stream("ticks")->interval == std::chrono::seconds(2));
    CHECK(d.find_stream("missing") == nullptr);
}

SBC_TEST(rpc_dispatcher_registers_event_streams) {
    EventHub hub;
    RpcDispatcher d;
    d.add_event_stream("config.subscribe", &hub,
                       []() -> asio::awaitable<ordered_json> { co_return ordered_json{{"k", 1}}; });
    CHECK(d.find_event_stream("config.subscribe") != nullptr);
    CHECK(d.find_event_stream("config.subscribe")->hub == &hub);
    CHECK(d.find_event_stream("missing") == nullptr);
    // Event streams are distinct from interval streams.
    CHECK(d.find_stream("config.subscribe") == nullptr);
}

SBC_TEST(event_hub_publishes_to_subscribers_on_their_executor) {
    EventHub hub;
    asio::io_context io;
    int got = 0;
    ordered_json last;
    auto id = hub.subscribe(io.get_executor(), [&](ordered_json data) {
        ++got;
        last = std::move(data);
    });
    hub.publish(ordered_json{{"v", 42}});
    io.run();  // the sink runs only when its executor is pumped
    CHECK(got == 1);
    CHECK(last.value("v", 0) == 42);

    // After unsubscribe, no further delivery.
    io.restart();
    hub.unsubscribe(id);
    hub.publish(ordered_json{{"v", 7}});
    io.run();
    CHECK(got == 1);
}
