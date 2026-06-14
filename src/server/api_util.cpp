#include "server/api_util.hpp"

namespace sbc::server {

namespace asio = boost::asio;

asio::awaitable<void> write_json(ResponseWriter& w, int status,
                                 const nlohmann::ordered_json& value) {
    HeaderMap headers;
    headers.set("Content-Type", "application/json; charset=utf-8");
    co_await w.write_full(status, std::move(headers), value.dump());
}

asio::awaitable<void> write_error(ResponseWriter& w, int status, const std::string& message) {
    nlohmann::ordered_json j;
    j["error"] = message;
    co_await write_json(w, status, j);
}

asio::awaitable<void> method_not_allowed(ResponseWriter& w) {
    co_await write_error(w, 405, "method not allowed");
}

asio::awaitable<void> not_found(ResponseWriter& w) {
    HeaderMap headers;
    headers.set("Content-Type", "text/plain; charset=utf-8");
    co_await w.write_full(404, std::move(headers), "404 page not found\n");
}

}  // namespace sbc::server
