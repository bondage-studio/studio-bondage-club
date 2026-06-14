#pragma once

#include <string>

#include <boost/asio/awaitable.hpp>

#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server {

inline constexpr const char* kServiceWorkerPath = "/studio-service-worker.js";

// serve_homepage_shell renders the admin-panel HTML shell that boots the React
// app and restores the original game homepage. `upstream`, `admin_base_path` and
// `local_game_server` (the boot-time default for the local/remote game-server
// switch) come from the active config.
boost::asio::awaitable<void> serve_homepage_shell(Request& req, ResponseWriter& w,
                                                  const std::string& upstream,
                                                  const std::string& admin_base_path,
                                                  bool local_game_server);

}  // namespace sbc::server
