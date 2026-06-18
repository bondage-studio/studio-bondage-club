#pragma once

#include <memory>
#include <string>

#include "include/cef_client.h"

#include "notifier.hpp"

namespace sbc::server {
class EmbeddedServer;
}

namespace sbc::desktop {

// SbcClient is the browser-process client for the app window. It is the desktop
// analogue of Android's MainActivity + LocalProxyWebViewClient:
//   - intercepts cross-origin GET/HEAD and routes them through the local
//     server's /<full-url> loader (cache + origin spoofing), so the page needs
//     no service worker;
//   - relays the native RPC bridge frames between the page (renderer) and
//     EmbeddedServer, whose deliver_rpc_frame verifies the capability token;
//   - quits the message loop when the last window closes.
class SbcClient : public CefClient,
                  public CefLifeSpanHandler,
                  public CefDisplayHandler,
                  public CefRequestHandler,
                  public CefResourceRequestHandler {
public:
    // |local_origin| is "http://host:port" (no trailing slash) for same-origin checks.
    SbcClient(sbc::server::EmbeddedServer* server, std::string local_origin);
    // Out-of-line so the CefRefPtr<CefBrowser> member is destroyed in
    // sbc_client.cpp, where CefBrowser is a complete type.
    ~SbcClient() override;

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // ApplyDesktopConfig resizes the live window to match a desktop-scope config
    // change (the live half of the desktop sync). UI thread only.
    void ApplyDesktopConfig(int width, int height);

    // CefDisplayHandler
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

    // CefRequestHandler
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
        bool is_navigation, bool is_download, const CefString& request_initiator,
        bool& disable_default_handling) override {
        return this;
    }
    // A main-frame navigation (reload, top-level load) replaces the page and its
    // RPC transport, so tear down the live native session: it drops the old page's
    // leaked subscription loops and guarantees the next page opens a fresh session.
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool user_gesture,
                        bool is_redirect) override;

    // CefResourceRequestHandler
    cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefRefPtr<CefRequest> request,
                                            CefRefPtr<CefCallback> callback) override;
    // Returns a CacheResourceHandler when the request is a confirmed fresh cache
    // HIT (served directly, bypassing the local-server loopback); otherwise null,
    // letting the request take the normal rewritten-loopback path. Runs on the IO
    // thread, after OnBeforeResourceLoad has rewritten cross-origin URLs onto the
    // local origin.
    CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser> browser,
                                                     CefRefPtr<CefFrame> frame,
                                                     CefRefPtr<CefRequest> request) override;

private:
    // Marshals one outbound RPC frame to the renderer. UI thread only.
    void SendFrameToRenderer(std::string frame);
    bool IsCrossOriginProxyable(const std::string& url, const std::string& method) const;

    sbc::server::EmbeddedServer* server_;
    std::string local_origin_;
    std::unique_ptr<Notifier> notifier_;
    CefRefPtr<CefBrowser> browser_;
    int browser_count_ = 0;

    IMPLEMENT_REFCOUNTING(SbcClient);
    DISALLOW_COPY_AND_ASSIGN(SbcClient);
};

}  // namespace sbc::desktop
