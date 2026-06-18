#pragma once

#include <cstddef>
#include <string>

#include "include/cef_resource_handler.h"

#include "host/provider.hpp"

namespace sbc::server {
class EmbeddedServer;
}

namespace sbc::desktop {

// CacheResourceHandler serves a confirmed cache HIT directly from the embedded
// server's store, bypassing the localhost loopback that the normal load path
// takes. It is created by SbcClient::GetResourceHandler only after
// EmbeddedServer::probe_cache has confirmed a fresh, unconditional HIT, so the
// response head is already known; the (possibly multi-MB) body is read lazily on
// a CEF file thread in Open() to keep the IO thread responsive, then streamed
// from memory by Read().
class CacheResourceHandler : public CefResourceHandler {
public:
    CacheResourceHandler(sbc::server::EmbeddedServer* server, host::CacheHit hit);

    bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
              CefRefPtr<CefCallback> callback) override;
    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& response_length,
                            CefString& redirectUrl) override;
    bool Skip(int64_t bytes_to_skip, int64_t& bytes_skipped,
              CefRefPtr<CefResourceSkipCallback> callback) override;
    bool Read(void* data_out, int bytes_to_read, int& bytes_read,
              CefRefPtr<CefResourceReadCallback> callback) override;
    void Cancel() override {}

private:
    // Runs on a CEF file thread: reads the body, then resumes the load.
    void ReadBodyThenContinue(CefRefPtr<CefCallback> callback);

    sbc::server::EmbeddedServer* server_;
    host::CacheHit hit_;
    std::string body_;
    std::size_t offset_ = 0;

    IMPLEMENT_REFCOUNTING(CacheResourceHandler);
    DISALLOW_COPY_AND_ASSIGN(CacheResourceHandler);
};

}  // namespace sbc::desktop