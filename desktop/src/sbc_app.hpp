#pragma once

#include "include/cef_app.h"

namespace sbc::server {
class EmbeddedServer;
}

namespace sbc::desktop {

class SbcClient;
class SbcRenderProcessHandler;

// SbcApp is the process-wide CefApp, constructed in every process before
// CefExecuteProcess. In the browser process it creates the top-level window
// once CEF is initialised; in renderer processes it returns the render-process
// handler that injects the native RPC bridge into each page.
class SbcApp : public CefApp, public CefBrowserProcessHandler {
public:
    SbcApp();
    // Out-of-line so the CefRefPtr<SbcRenderProcessHandler> member is destroyed
    // in sbc_app.cpp, where the type is complete (it is only forward-declared here).
    ~SbcApp() override;

    // Called in the browser process before CefInitialize: hands over the client
    // and the URL the first window should load. Unused in subprocesses.
    void SetBrowserConfig(CefRefPtr<SbcClient> client, const CefString& url);

    // SetDesktopConfig hands over the start-time desktop settings (read from the
    // config) plus the server for the reverse window-size path. Browser process
    // only; called after the embedded server starts and before CefInitialize.
    void SetDesktopConfig(server::EmbeddedServer* server, bool hardware_acceleration,
                          int window_width, int window_height);

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
    // Disable GPU compositing when desktop.hardwareAcceleration is off (browser
    // process only; Chromium propagates the resulting switch to subprocesses).
    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

private:
    CefRefPtr<SbcClient> client_;
    CefString url_;
    CefRefPtr<SbcRenderProcessHandler> render_handler_;
    server::EmbeddedServer* server_ = nullptr;
    bool hardware_acceleration_ = true;
    int window_width_ = 1280;
    int window_height_ = 800;

    IMPLEMENT_REFCOUNTING(SbcApp);
    DISALLOW_COPY_AND_ASSIGN(SbcApp);
};

}  // namespace sbc::desktop
