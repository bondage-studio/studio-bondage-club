#pragma once

#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

namespace sbc::desktop {

// SbcWindowDelegate owns the top-level window hosting the browser view: it
// parents the view, sizes and shows the window, and routes the window-close
// gesture through the browser's clean shutdown (TryCloseBrowser).
class SbcWindowDelegate : public CefWindowDelegate {
public:
    explicit SbcWindowDelegate(CefRefPtr<CefBrowserView> browser_view);

    void OnWindowCreated(CefRefPtr<CefWindow> window) override;
    void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
    bool CanClose(CefRefPtr<CefWindow> window) override;
    CefSize GetPreferredSize(CefRefPtr<CefView> view) override;

private:
    CefRefPtr<CefBrowserView> browser_view_;

    IMPLEMENT_REFCOUNTING(SbcWindowDelegate);
    DISALLOW_COPY_AND_ASSIGN(SbcWindowDelegate);
};

}  // namespace sbc::desktop
