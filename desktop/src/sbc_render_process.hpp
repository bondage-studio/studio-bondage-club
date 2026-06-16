#pragma once

#include <map>

#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

namespace sbc::desktop {

// SbcRenderProcessHandler runs in renderer processes. It injects
// window.__sbcNativeRpc into each main-frame V8 context — the same
// { postMessage(string), onmessage } shape the Android hosts expose (see
// android/.../gecko/.../content.js) — and relays RPC frames to/from the browser
// process over CefProcessMessage IPC. The web bundle's nativeBridge.ts captures
// the object identically and is unaware it runs under CEF.
class SbcRenderProcessHandler : public CefRenderProcessHandler {
public:
    SbcRenderProcessHandler() = default;

    void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

private:
    // The bridge object + its context, kept per browser (main frame only) so
    // native->page frames can invoke the page's onmessage handler even after the
    // page deletes window.__sbcNativeRpc (nativeBridge.ts erases the global but
    // keeps its own reference to the same object).
    struct Bridge {
        CefRefPtr<CefV8Context> context;
        CefRefPtr<CefV8Value> object;
    };
    std::map<int, Bridge> bridges_;

    IMPLEMENT_REFCOUNTING(SbcRenderProcessHandler);
    DISALLOW_COPY_AND_ASSIGN(SbcRenderProcessHandler);
};

}  // namespace sbc::desktop
