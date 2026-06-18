#pragma once

namespace sbc::net {
class IoRuntime;
class BlockingPool;
class TlsContext;
}  // namespace sbc::net

namespace sbc::cache {
class TrafficStats;
}

namespace sbc::host {

// ProviderContext carries shared runtime dependencies into providers, so adding
// a new dependency does not churn every constructor signature.
struct ProviderContext {
    net::IoRuntime* io = nullptr;
    net::BlockingPool* blocking = nullptr;
    net::TlsContext* tls = nullptr;
    // Process-lifetime cache-traffic collector (owned by App), so per-host hit
    // rates survive provider rebuilds on config changes. May be null.
    cache::TrafficStats* traffic = nullptr;
};

}  // namespace sbc::host
