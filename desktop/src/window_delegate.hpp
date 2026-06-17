#pragma once

#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

namespace sbc::server {
class EmbeddedServer;
}

namespace sbc::desktop {

// SbcWindowDelegate owns the top-level window hosting the browser view: it
// parents the view, sizes and shows the window, and routes the window-close
// gesture through the browser's clean shutdown (TryCloseBrowser). It seeds the
// initial size from the desktop config and (when remember_window_size is on)
// writes user resizes back into the config via the server.
class SbcWindowDelegate : public CefWindowDelegate {
public:
    SbcWindowDelegate(CefRefPtr<CefBrowserView> browser_view, int width, int height,
                      server::EmbeddedServer* server);

    void OnWindowCreated(CefRefPtr<CefWindow> window) override;
    void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
    void OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect& new_bounds) override;
    bool CanClose(CefRefPtr<CefWindow> window) override;
    CefSize GetPreferredSize(CefRefPtr<CefView> view) override;

private:
    CefRefPtr<CefBrowserView> browser_view_;
    int width_;
    int height_;
    server::EmbeddedServer* server_;

    IMPLEMENT_REFCOUNTING(SbcWindowDelegate);
    DISALLOW_COPY_AND_ASSIGN(SbcWindowDelegate);
};

}  // namespace sbc::desktop
