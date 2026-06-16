#include "sbc_render_process.hpp"

#include "include/cef_frame.h"

#include "sbc_ipc.hpp"

namespace sbc::desktop {

namespace {

constexpr char kBridgeName[] = "__sbcNativeRpc";

// PostMessageHandler backs __sbcNativeRpc.postMessage(string): it forwards the
// frame to the browser process, where EmbeddedServer::deliver_rpc_frame consumes
// it (verifying the capability token carried in the frame).
class PostMessageHandler : public CefV8Handler {
public:
    PostMessageHandler() = default;

    bool Execute(const CefString& name, CefRefPtr<CefV8Value> /*object*/,
                 const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& /*retval*/,
                 CefString& /*exception*/) override {
        if (name == "postMessage" && arguments.size() == 1 && arguments[0]->IsString()) {
            CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
            if (context && context->GetFrame()) {
                CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kRpcToBrowser);
                message->GetArgumentList()->SetString(0, arguments[0]->GetStringValue());
                context->GetFrame()->SendProcessMessage(PID_BROWSER, message);
            }
        }
        return true;
    }

private:
    IMPLEMENT_REFCOUNTING(PostMessageHandler);
    DISALLOW_COPY_AND_ASSIGN(PostMessageHandler);
};

}  // namespace

void SbcRenderProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                               CefRefPtr<CefFrame> frame,
                                               CefRefPtr<CefV8Context> context) {
    if (!frame->IsMain()) {
        return;
    }

    CefRefPtr<CefV8Value> bridge = CefV8Value::CreateObject(nullptr, nullptr);
    CefRefPtr<CefV8Handler> handler = new PostMessageHandler();
    bridge->SetValue("postMessage", CefV8Value::CreateFunction("postMessage", handler),
                     V8_PROPERTY_ATTRIBUTE_NONE);
    // A plain writable slot the page sets its handler into; we read it back at
    // delivery time. Defaults to null, matching the bridge contract.
    bridge->SetValue("onmessage", CefV8Value::CreateNull(), V8_PROPERTY_ATTRIBUTE_NONE);

    context->GetGlobal()->SetValue(kBridgeName, bridge, V8_PROPERTY_ATTRIBUTE_NONE);
    bridges_[browser->GetIdentifier()] = {context, bridge};
}

void SbcRenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                CefRefPtr<CefV8Context> /*context*/) {
    if (!frame->IsMain()) {
        return;
    }
    bridges_.erase(browser->GetIdentifier());
}

bool SbcRenderProcessHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                                       CefRefPtr<CefFrame> /*frame*/,
                                                       CefProcessId source_process,
                                                       CefRefPtr<CefProcessMessage> message) {
    if (source_process != PID_BROWSER || message->GetName() != kRpcToPage) {
        return false;
    }

    auto it = bridges_.find(browser->GetIdentifier());
    if (it == bridges_.end()) {
        return true;
    }
    CefRefPtr<CefV8Context> context = it->second.context;
    CefRefPtr<CefV8Value> bridge = it->second.object;
    const CefString frame_json = message->GetArgumentList()->GetString(0);

    if (!context->Enter()) {
        return true;
    }
    CefRefPtr<CefV8Value> onmessage = bridge->GetValue("onmessage");
    if (onmessage && onmessage->IsFunction()) {
        // Deliver as { data: "<json>" }, mirroring the WebMessage/Port event shape.
        CefRefPtr<CefV8Value> event = CefV8Value::CreateObject(nullptr, nullptr);
        event->SetValue("data", CefV8Value::CreateString(frame_json), V8_PROPERTY_ATTRIBUTE_NONE);
        CefV8ValueList args;
        args.push_back(event);
        onmessage->ExecuteFunction(bridge, args);
    }
    context->Exit();
    return true;
}

}  // namespace sbc::desktop
