#pragma once

#include <string>
#include <string_view>

namespace sbc::server::rpc {

// RpcAuth holds a single process-lifetime capability secret for the /rpc channel.
//
// The secret is embedded once in the homepage bootstrap, where the trusted client
// reads it at document-start and erases it from the DOM before any injected
// userscript runs. It is then presented once at the WebSocket `hello` handshake.
//
// This is defense-in-depth layered on top of same-origin: a fresh 32-byte secret
// per process start, deliberately *not* stored in a table and *not* expiring
// (per design). Verification is a constant-time compare.
class RpcAuth {
public:
    RpcAuth();

    const std::string& token() const { return token_; }

    bool verify(std::string_view candidate) const;

private:
    std::string token_;
};

}  // namespace sbc::server::rpc
