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

// NotifyHandler backs __sbcNotify(title, body, tag): it forwards a notification
// request to the browser process, where SbcClient presents it via the OS. The
// page never calls this directly — the injected shim below routes the standard
// window.Notification API through it.
class NotifyHandler : public CefV8Handler {
public:
    NotifyHandler() = default;

    bool Execute(const CefString& name, CefRefPtr<CefV8Value> /*object*/,
                 const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& /*retval*/,
                 CefString& /*exception*/) override {
        if (name == "notify" && arguments.size() == 3 && arguments[0]->IsString() &&
            arguments[1]->IsString() && arguments[2]->IsString()) {
            CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
            if (context && context->GetFrame()) {
                CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kNotifyShow);
                CefRefPtr<CefListValue> args = message->GetArgumentList();
                args->SetString(0, arguments[0]->GetStringValue());
                args->SetString(1, arguments[1]->GetStringValue());
                args->SetString(2, arguments[2]->GetStringValue());
                context->GetFrame()->SendProcessMessage(PID_BROWSER, message);
            }
        }
        return true;
    }

private:
    IMPLEMENT_REFCOUNTING(NotifyHandler);
    DISALLOW_COPY_AND_ASSIGN(NotifyHandler);
};

// Replaces window.Notification with a shim that routes through __sbcNotify (set
// just before this runs). CEF's runtime presents no notifications and the native
// permission flow is unavailable, so permission is reported as already granted
// and each `new Notification(...)` is forwarded to the host. Runs at context
// creation, before the page's own scripts, so the game only ever sees the shim.
constexpr char kNotificationShim[] = R"JS(
(() => {
  const _notify = window.__sbcNotify;
  try { delete window.__sbcNotify; } catch (e) {}
  if (typeof _notify !== 'function') return;

  class SbcNotification extends EventTarget {
    constructor(title, options) {
      super();
      options = options || {};
      this.title = String(title);
      this.body = options.body != null ? String(options.body) : '';
      this.tag = options.tag != null ? String(options.tag) : '';
      this.icon = options.icon != null ? String(options.icon) : '';
      this.dir = options.dir || 'auto';
      this.lang = options.lang || '';
      this.data = options.data != null ? options.data : null;
      this.onclick = null;
      this.onshow = null;
      this.onclose = null;
      this.onerror = null;
      try { _notify(this.title, this.body, this.tag); } catch (e) {}
      // Match the spec's async "show" dispatch so listeners attached right after
      // construction still fire.
      Promise.resolve().then(() => {
        const ev = new Event('show');
        if (typeof this.onshow === 'function') { try { this.onshow(ev); } catch (e) {} }
        this.dispatchEvent(ev);
      });
    }
    // The host owns the notification's lifetime; locally we just emit 'close'.
    close() {
      Promise.resolve().then(() => {
        const ev = new Event('close');
        if (typeof this.onclose === 'function') { try { this.onclose(ev); } catch (e) {} }
        this.dispatchEvent(ev);
      });
    }
    static requestPermission(cb) {
      if (typeof cb === 'function') { try { cb('granted'); } catch (e) {} }
      return Promise.resolve('granted');
    }
  }
  Object.defineProperty(SbcNotification, 'permission', {
    get() { return 'granted'; }, configurable: true,
  });
  Object.defineProperty(SbcNotification, 'maxActions', {
    get() { return 0; }, configurable: true,
  });
  Object.defineProperty(window, 'Notification', {
    value: SbcNotification, writable: true, configurable: true,
  });
})();
)JS";

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

    // Expose __sbcNotify, then install the window.Notification shim that routes
    // through it. CEF presents no OS notifications on its own, so without this the
    // game's notifications would be silently dropped.
    context->GetGlobal()->SetValue("__sbcNotify",
                                   CefV8Value::CreateFunction("notify", new NotifyHandler()),
                                   V8_PROPERTY_ATTRIBUTE_NONE);
    frame->ExecuteJavaScript(kNotificationShim, frame->GetURL(), 0);
}

void SbcRenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                CefRefPtr<CefV8Context> context) {
    if (!frame->IsMain()) {
        return;
    }
    // On a reload/navigation CEF may create the new document's context before
    // releasing the old one. Both main-frame contexts share this browser-id key,
    // so a blind erase here would drop the *new* page's bridge — and with it every
    // inbound RPC response (the page hangs at homepage.get, since the native
    // transport has no retry). Only erase when the entry still refers to the
    // context actually being released.
    auto it = bridges_.find(browser->GetIdentifier());
    if (it != bridges_.end() && it->second.context->IsSame(context)) {
        bridges_.erase(it);
    }
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
