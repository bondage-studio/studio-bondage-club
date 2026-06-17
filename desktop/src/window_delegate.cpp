#include "window_delegate.hpp"

#include "include/cef_browser.h"

#include "server/embedded_server.hpp"

namespace sbc::desktop {

SbcWindowDelegate::SbcWindowDelegate(CefRefPtr<CefBrowserView> browser_view, int width, int height,
                                     server::EmbeddedServer* server)
    : browser_view_(browser_view), width_(width), height_(height), server_(server) {}

void SbcWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
    window->AddChildView(browser_view_);
    window->SetTitle("Studio Bondage Club");
    window->Show();
    browser_view_->RequestFocus();
}

void SbcWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> /*window*/) {
    browser_view_ = nullptr;
}

void SbcWindowDelegate::OnWindowBoundsChanged(CefRefPtr<CefWindow> /*window*/,
                                              const CefRect& new_bounds) {
    // Persist the new size into config. apply_desktop_window_size no-ops when the
    // size is unchanged or remember_window_size is off, so the echo back from
    // config.subscribe (which re-sets the same size) doesn't loop.
    if (server_ != nullptr) server_->update_desktop_window_size(new_bounds.width, new_bounds.height);
}

bool SbcWindowDelegate::CanClose(CefRefPtr<CefWindow> /*window*/) {
    // Let the browser tear down cleanly (fires OnBeforeClose, which quits the
    // message loop once the last browser is gone).
    CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
    return browser ? browser->GetHost()->TryCloseBrowser() : true;
}

CefSize SbcWindowDelegate::GetPreferredSize(CefRefPtr<CefView> /*view*/) {
    return CefSize(width_, height_);
}

}  // namespace sbc::desktop
