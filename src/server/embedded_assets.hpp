#pragma once

#include <memory>

#include "server/static_assets.hpp"

namespace sbc::server {

// make_embedded_asset_source returns an AssetSource backed by web/dist files
// compiled into the binary. Defined in the generated translation unit produced
// by cmake/EmbedWebAssets.cmake (only built when SBC_EMBED_WEB=ON).
std::shared_ptr<AssetSource> make_embedded_asset_source();

}  // namespace sbc::server
