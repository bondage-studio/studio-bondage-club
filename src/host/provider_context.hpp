#pragma once

namespace sbc::net {
class IoRuntime;
class BlockingPool;
class TlsContext;
}  // namespace sbc::net

namespace sbc::host {

// ProviderContext carries shared runtime dependencies into providers, so adding
// a new dependency does not churn every constructor signature.
struct ProviderContext {
    net::IoRuntime* io = nullptr;
    net::BlockingPool* blocking = nullptr;
    net::TlsContext* tls = nullptr;
};

}  // namespace sbc::host
