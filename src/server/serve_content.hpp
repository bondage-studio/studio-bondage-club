#pragma once

#include <optional>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "cache/metadata.hpp"
#include "common/http_util.hpp"
#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server {

// serve_content serves an in-memory body with conditional requests, single-range
// requests, Accept-Ranges, and HEAD handling. `headers` are sent as-is (the caller
// has already set Content-Type and any X-Studio-* headers). ETag is read from
// `headers`.
boost::asio::awaitable<void> serve_content(Request& req, ResponseWriter& w, HeaderMap headers,
                                           std::string body,
                                           std::optional<cache::TimePoint> mod_time);

}  // namespace sbc::server
