#pragma once

#include <string>

#include <boost/asio/awaitable.hpp>

#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server {

inline constexpr const char* kServiceWorkerPath = "/studio-service-worker.js";

// serve_homepage_shell renders the admin-panel HTML shell that boots the React
// app and restores the game homepage. `upstream`, `admin_base_path` and
// `local_game_server` come from the active config. `rpc_token` is the capability
// secret embedded for the trusted client to read (and erase) at document-start.
boost::asio::awaitable<void> serve_homepage_shell(Request& req, ResponseWriter& w,
                                                  const std::string& upstream,
                                                  const std::string& admin_base_path,
                                                  bool local_game_server,
                                                  const std::string& rpc_token);

}  // namespace sbc::server
