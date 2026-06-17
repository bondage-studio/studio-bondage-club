#include "server/rpc/event_hub.hpp"

#include <utility>
#include <vector>

#include <boost/asio/post.hpp>

namespace sbc::server::rpc {

namespace asio = boost::asio;

EventHub::SubId EventHub::subscribe(asio::any_io_executor exec, Sink sink) {
    std::lock_guard<std::mutex> lock(mu_);
    SubId id = next_++;
    subs_.emplace(id, Sub{std::move(exec), std::move(sink)});
    return id;
}

void EventHub::unsubscribe(SubId id) {
    std::lock_guard<std::mutex> lock(mu_);
    subs_.erase(id);
}

void EventHub::publish(const nlohmann::ordered_json& event) {
    std::vector<Sub> targets;
    {
        std::lock_guard<std::mutex> lock(mu_);
        targets.reserve(subs_.size());
        for (const auto& [id, sub] : subs_) targets.push_back(sub);
    }
    // Post outside the lock so a sink can never re-enter the hub on the same stack
    // and so a slow strand never stalls publishers.
    for (auto& t : targets) {
        asio::post(t.exec, [sink = t.sink, event]() mutable { sink(std::move(event)); });
    }
}

}  // namespace sbc::server::rpc
