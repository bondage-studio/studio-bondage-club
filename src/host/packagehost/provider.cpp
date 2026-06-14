#include "host/packagehost/provider.hpp"

#include "server/api_util.hpp"

namespace sbc::host::packagehost {

namespace asio = boost::asio;

std::shared_ptr<Provider> Provider::create(const config::Config& cfg,
                                           const host::ProviderContext& ctx) {
    (void)ctx;
    auto p = std::make_shared<Provider>();
    p->package_dir_ = cfg.package.dir;
    return p;
}

asio::awaitable<void> Provider::serve(server::Request& req, server::ResponseWriter& w) {
    (void)req;
    co_await server::write_error(w, 501, "package cache mode is not implemented");
}

host::RuntimeStatus Provider::status() const {
    host::RuntimeStatus s;
    s.mode = config::kModePackage;
    s.capabilities = {
        {"package-cache", "Package cache", false,
         "Reserved for local plugin/package bundles and binary diff update workflows."},
    };
    return s;
}

}  // namespace sbc::host::packagehost
