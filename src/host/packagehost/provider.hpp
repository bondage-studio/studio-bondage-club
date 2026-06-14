#pragma once

#include <memory>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "config/config.hpp"
#include "host/provider.hpp"
#include "host/provider_context.hpp"

namespace sbc::host::packagehost {

// Provider is a placeholder for the future package/plugin cache mode. It reports
// a single disabled capability and returns 501 for all serving.
class Provider : public host::Provider {
public:
    static std::shared_ptr<Provider> create(const config::Config& cfg,
                                            const host::ProviderContext& ctx);

    boost::asio::awaitable<void> serve(server::Request& req, server::ResponseWriter& w) override;
    host::RuntimeStatus status() const override;

private:
    std::string package_dir_;
};

}  // namespace sbc::host::packagehost
