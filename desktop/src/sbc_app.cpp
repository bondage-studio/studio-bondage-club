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

void SbcApp::SetDesktopConfig(server::EmbeddedServer* server, bool hardware_acceleration,
                             int window_width, int window_height) {
    server_ = server;
    hardware_acceleration_ = hardware_acceleration;
    window_width_ = window_width;
    window_height_ = window_height;
}

CefRefPtr<CefRenderProcessHandler> SbcApp::GetRenderProcessHandler() {
    return render_handler_;
}

void SbcApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                           CefRefPtr<CefCommandLine> command_line) {
    // Only the browser process (empty type) carries our config-derived switch;
    // Chromium forwards GPU-disabling to the child processes it spawns.
    if (!process_type.empty() || hardware_acceleration_) return;
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
}

void SbcApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    // Views framework: a CefBrowserView hosting the page, parented to a native
    // top-level window. No GTK dependency, cross-platform.
    CefBrowserSettings browser_settings;
    CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
        client_, url_, browser_settings, /*extra_info=*/nullptr,
        /*request_context=*/nullptr, /*delegate=*/nullptr);
    CefWindow::CreateTopLevelWindow(
        new SbcWindowDelegate(browser_view, window_width_, window_height_, server_));
}

}  // namespace sbc::desktop
