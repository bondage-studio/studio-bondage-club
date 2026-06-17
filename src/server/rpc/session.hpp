#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

#include "server/rpc/dispatcher.hpp"
#include "server/rpc/event_hub.hpp"

namespace sbc::server::rpc {

class RpcDispatcher;

// RpcSession is the transport-agnostic half of the /rpc protocol: it turns
// inbound `req`/`sub`/`unsub` frames into dispatcher calls and subscription
// tickers, emitting `res`/`event` frames through a Sender callback. It knows
// nothing about WebSockets or JNI — the carrier (RpcConnection on web,
// EmbeddedServer's native bridge on Android) owns framing, auth and the wire.
//
// All state (the subscription map, the stop flag) is touched only on the
// executor passed at construction, which MUST be a serialising executor (a
// strand). Carriers running off-strand (the native bridge) post onto
// executor() before calling handle_frame()/cancel_all(); the WS connection is
// already on its strand and calls them directly. The Sender is likewise invoked
// on that executor, so a WS carrier can enqueue straight into its outbox.
class RpcSession : public std::enable_shared_from_this<RpcSession> {
public:
    using Sender = std::function<void(std::string)>;

    RpcSession(boost::asio::any_io_executor exec, const RpcDispatcher& dispatcher, Sender send);

    // The executor every method below must run on. Off-strand carriers post here.
    boost::asio::any_io_executor executor() const { return exec_; }

    // handle_frame processes one already-parsed, already-authenticated frame
    // (the carrier strips any transport-level token first). Unknown `t` values
    // are ignored. Precondition: invoked on executor().
    void handle_frame(const nlohmann::ordered_json& msg);

    // cancel_all stops every live subscription and marks the session stopped so
    // in-flight tickers exit. Precondition: invoked on executor().
    void cancel_all();

private:
    void start_subscription(std::int64_t id, const std::string& method);
    void start_event_subscription(std::int64_t id, const RpcDispatcher::EventStream& stream);
    void stop_subscription(std::int64_t id);

    boost::asio::any_io_executor exec_;
    const RpcDispatcher& dispatcher_;
    Sender send_;

    // Interval-driven subscriptions (timer per id) and event-driven ones (an
    // EventHub registration per id). A given id lives in exactly one map.
    std::map<std::int64_t, std::shared_ptr<boost::asio::steady_timer>> subs_;
    std::map<std::int64_t, std::pair<EventHub*, EventHub::SubId>> event_subs_;
    bool stopped_ = false;
};

}  // namespace sbc::server::rpc