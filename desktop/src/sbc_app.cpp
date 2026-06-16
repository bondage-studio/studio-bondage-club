#include "sbc_app.hpp"

#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include "sbc_client.hpp"
#include "sbc_render_process.hpp"
#include "window_delegate.hpp"

namespace sbc::desktop {

SbcApp::SbcApp() : render_handler_(new SbcRenderProcessHandler()) {}

SbcApp::~SbcApp() = default;

void SbcApp::SetBrowserConfig(CefRefPtr<SbcClient> client, const CefString& url) {
    client_ = client;
    url_ = url;
}

CefRefPtr<CefRenderProcessHandler> SbcApp::GetRenderProcessHandler() {
    return render_handler_;
}

void SbcApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    // Views framework: a CefBrowserView hosting the page, parented to a native
    // top-level window. No GTK dependency, cross-platform.
    CefBrowserSettings browser_settings;
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        client_, url_, browser_settings, /*extra_info=*/nullptr,
        /*request_context=*/nullptr, /*delegate=*/nullptr);
    CefWindow::CreateTopLevelWindow(new SbcWindowDelegate(browser_view));
}

}  // namespace sbc::desktop
