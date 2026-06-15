#pragma once

#include <memory>
#include <optional>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "server/http_types.hpp"
#include "server/response_writer.hpp"

namespace sbc::server {

// AssetSource abstracts where web/dist assets come from (filesystem now,
// compiled-in virtual FS when SBC_EMBED_WEB is set). Paths are dist-relative
// and forward-slash separated (e.g. "assets/studio-panel.js").
class AssetSource {
public:
    virtual ~AssetSource() = default;
    virtual std::optional<std::string> read(const std::string& rel_path) = 0;
    virtual bool available() const = 0;
};

// default_asset_source returns the build-selected source (embedded or
// filesystem rooted at $SBC_WEB_ROOT or ./web/dist).
std::shared_ptr<AssetSource> default_asset_source();

std::string content_type_for(const std::string& filename);

boost::asio::awaitable<void> serve_web_asset(Request& req, ResponseWriter& w,
                                             const std::string& base_path, AssetSource& source);

boost::asio::awaitable<void> serve_service_worker(Request& req, ResponseWriter& w,
                                                  AssetSource& source);

}  // namespace sbc::server
