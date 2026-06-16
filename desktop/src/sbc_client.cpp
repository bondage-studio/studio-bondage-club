#include "sbc_client.hpp"

#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "sbc_ipc.hpp"
#include "server/embedded_server.hpp"

namespace sbc::desktop {

SbcClient::SbcClient(sbc::server::EmbeddedServer* server, std::string local_origin)
    : server_(server), local_origin_(std::move(local_origin)) {}

SbcClient::~SbcClient() = default;

void SbcClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    ++browser_count_;
    if (browser_) {
        return;
    }
    browser_ = browser;

    // Wire the native RPC outbound sink. It fires on Asio worker threads, so hop
    // to the UI thread before touching the browser/IPC. The captured CefRefPtr
    // keeps this client alive for as long as the server holds the sender.
    CefRefPtr<SbcClient> self = this;
    server_->set_rpc_sender([self](std::string frame) {
        CefPostTask(TID_UI,
                    base::BindOnce(&SbcClient::SendFrameToRenderer, self, std::move(frame)));
    });
}

void SbcClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (browser_ && browser_->IsSame(browser)) {
        // Tear the native session down before the browser goes away so no further
        // outbound frames are posted toward a dead renderer.
        server_->reset_rpc();
        browser_ = nullptr;
    }
    if (--browser_count_ == 0) {
        CefQuitMessageLoop();
    }
}

void SbcClient::SendFrameToRenderer(std::string frame) {
    CEF_REQUIRE_UI_THREAD();
    if (!browser_) {
        return;
    }
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kRpcToPage);
    message->GetArgumentList()->SetString(0, frame);
    browser_->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

bool SbcClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> /*browser*/,
                                         CefRefPtr<CefFrame> /*frame*/, CefProcessId source_process,
                                         CefRefPtr<CefProcessMessage> message) {
    if (source_process != PID_RENDERER || message->GetName() != kRpcToBrowser) {
        return false;
    }
    // The frame carries its own capability token; deliver_rpc_frame verifies it
    // and is safe to call from any thread.
    server_->deliver_rpc_frame(message->GetArgumentList()->GetString(0).ToString());
    return true;
}

void SbcClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
    if (view && view->GetWindow()) {
        view->GetWindow()->SetTitle(title);
    }
}

cef_return_value_t SbcClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser> /*browser*/,
                                                   CefRefPtr<CefFrame> /*frame*/,
                                                   CefRefPtr<CefRequest> request,
                                                   CefRefPtr<CefCallback> /*callback*/) {
    const std::string url = request->GetURL().ToString();
    const std::string method = request->GetMethod().ToString();
    if (IsCrossOriginProxyable(url, method)) {
        // Route through the local loader: GET http://host:port/<full-url>. The
        // server proxies + caches it and spoofs Origin/Referer upstream. Mirrors
        // android LocalProxyWebViewClient.proxyThroughLocalServer.
        request->SetURL(local_origin_ + "/" + url);
    }
    return RV_CONTINUE;
}

bool SbcClient::IsCrossOriginProxyable(const std::string& url, const std::string& method) const {
    if (method != "GET" && method != "HEAD") {
        return false;
    }
    // Only http(s) subresources are proxyable (ws/data/blob/chrome pass through).
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        return false;
    }
    // Same-origin requests — including the loader URL we just rewrote to — start
    // with the local origin and must pass through untouched.
    if (url.rfind(local_origin_, 0) == 0) {
        return false;
    }
    return true;
}

}  // namespace sbc::desktop
