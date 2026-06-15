#pragma once

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

namespace sbc::server::rpc {

// RpcError carries a machine-readable code alongside a human message. A method
// handler throws it to return {"ok":false,"error":{code,message}}; the dispatcher
// throws it for unknown methods. Any other std::exception maps to code "internal".
struct RpcError : std::runtime_error {
    std::string code;
    RpcError(std::string code_, const std::string& message)
        : std::runtime_error(message), code(std::move(code_)) {}
};

// RpcDispatcher is a transport-agnostic method registry. Unary methods map a JSON
// params object to a JSON result (both via coroutines, so handlers can offload to
// the blocking pool). Stream methods produce one JSON payload per tick at a fixed
// interval; the connection owns the ticking and pushes each as an `event` frame.
class RpcDispatcher {
public:
    using Method = std::function<boost::asio::awaitable<nlohmann::ordered_json>(
        const nlohmann::ordered_json&)>;
    using StreamProducer = std::function<boost::asio::awaitable<nlohmann::ordered_json>()>;
    struct Stream {
        StreamProducer produce;
        std::chrono::milliseconds interval;
    };

    void add(std::string name, Method fn);
    void add_stream(std::string name, StreamProducer fn, std::chrono::milliseconds interval);

    // call dispatches a unary method. Throws RpcError("method_not_found") when the
    // method is unknown; otherwise propagates whatever the handler throws.
    boost::asio::awaitable<nlohmann::ordered_json> call(const std::string& method,
                                                        const nlohmann::ordered_json& params) const;

    const Stream* find_stream(const std::string& method) const;

private:
    std::unordered_map<std::string, Method> methods_;
    std::unordered_map<std::string, Stream> streams_;
};

}  // namespace sbc::server::rpc
