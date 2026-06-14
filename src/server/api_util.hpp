#pragma once

#include <string>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "server/response_writer.hpp"

namespace sbc::server {

// write_json sends a buffered JSON response with the standard content type.
boost::asio::awaitable<void> write_json(ResponseWriter& w, int status,
                                        const nlohmann::ordered_json& value);

// write_error sends {"error": message} with the given status.
boost::asio::awaitable<void> write_error(ResponseWriter& w, int status, const std::string& message);

// method_not_allowed sends a 405 JSON error.
boost::asio::awaitable<void> method_not_allowed(ResponseWriter& w);

// not_found sends a 404 plain-text response (mirrors http.NotFound).
boost::asio::awaitable<void> not_found(ResponseWriter& w);

}  // namespace sbc::server
