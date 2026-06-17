#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <nlohmann/json.hpp>

namespace sbc::server::rpc {

// EventHub fans a server-originated event out to all currently-subscribed
// sessions. Unlike interval streams (driven inside RpcSession by a timer), events
// are pushed only when a producer calls publish(). Each subscriber registers a
// sink (a session's send bound to a subscription id) plus the executor that sink
// must run on; publish() posts the event onto each subscriber's executor, so the
// sink is always touched on its own strand. Thread-safe: publish() may be called
// from any thread (e.g. the config-apply coroutine).
class EventHub {
public:
    using SubId = std::uint64_t;
    using Sink = std::function<void(nlohmann::ordered_json)>;

    // subscribe registers a sink and returns a token the caller stores and passes
    // to unsubscribe() on stream-stop / session teardown.
    SubId subscribe(boost::asio::any_io_executor exec, Sink sink);
    void unsubscribe(SubId id);

    // publish snapshots the subscriber list under the lock, then posts a copy of
    // `event` onto each subscriber's executor outside the lock.
    void publish(const nlohmann::ordered_json& event);

private:
    struct Sub {
        boost::asio::any_io_executor exec;
        Sink sink;
    };
    std::mutex mu_;
    SubId next_ = 1;
    std::unordered_map<SubId, Sub> subs_;
};

}  // namespace sbc::server::rpc
