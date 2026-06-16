#include "window_delegate.hpp"

#include "include/cef_browser.h"

namespace sbc::desktop {

SbcWindowDelegate::SbcWindowDelegate(CefRefPtr<CefBrowserView> browser_view)
    : browser_view_(browser_view) {}

void SbcWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
    window->AddChildView(browser_view_);
    window->SetTitle("Studio Bondage Club");
    window->Show();
    browser_view_->RequestFocus();
}

void SbcWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> /*window*/) {
    browser_view_ = nullptr;
}

bool SbcWindowDelegate::CanClose(CefRefPtr<CefWindow> /*window*/) {
    // Let the browser tear down cleanly (fires OnBeforeClose, which quits the
    // message loop once the last browser is gone).
    CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
    return browser ? browser->GetHost()->TryCloseBrowser() : true;
}

CefSize SbcWindowDelegate::GetPreferredSize(CefRefPtr<CefView> /*view*/) {
    return CefSize(1280, 800);
}

}  // namespace sbc::desktop
