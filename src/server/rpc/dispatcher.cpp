#include "server/rpc/dispatcher.hpp"

namespace sbc::server::rpc {

void RpcDispatcher::add(std::string name, Method fn) {
    methods_[std::move(name)] = std::move(fn);
}

void RpcDispatcher::add_stream(std::string name, StreamProducer fn,
                               std::chrono::milliseconds interval) {
    streams_[std::move(name)] = Stream{std::move(fn), interval};
}

void RpcDispatcher::add_event_stream(std::string name, EventHub* hub, StreamProducer initial) {
    event_streams_[std::move(name)] = EventStream{hub, std::move(initial)};
}

boost::asio::awaitable<nlohmann::ordered_json> RpcDispatcher::call(
    const std::string& method, const nlohmann::ordered_json& params) const {
    auto it = methods_.find(method);
    if (it == methods_.end()) throw RpcError("method_not_found", "unknown RPC method: " + method);
    co_return co_await it->second(params);
}

const RpcDispatcher::Stream* RpcDispatcher::find_stream(const std::string& method) const {
    auto it = streams_.find(method);
    return it == streams_.end() ? nullptr : &it->second;
}

const RpcDispatcher::EventStream* RpcDispatcher::find_event_stream(const std::string& method) const {
    auto it = event_streams_.find(method);
    return it == event_streams_.end() ? nullptr : &it->second;
}

}  // namespace sbc::server::rpc
