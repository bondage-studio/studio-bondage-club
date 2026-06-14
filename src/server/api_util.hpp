#pragma once

#include <string>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "server/response_writer.hpp"

namespace sbc::server {

boost::asio::awaitable<void> write_json(ResponseWriter& w, int status,
                                        const nlohmann::ordered_json& value);

boost::asio::awaitable<void> write_error(ResponseWriter& w, int status, const std::string& message);

boost::asio::awaitable<void> method_not_allowed(ResponseWriter& w);

boost::asio::awaitable<void> not_found(ResponseWriter& w);

}  // namespace sbc::server
