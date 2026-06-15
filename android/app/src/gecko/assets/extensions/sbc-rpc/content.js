// Studio BC RPC bridge — GeckoView content script.
//
// Runs at document_start (before the page's own scripts) in the extension's
// isolated world. It opens a native-messaging port to the embedder app and
// exports a page-world bridge object — window.__sbcNativeRpc with the same
// { postMessage(string), onmessage } shape as the system flavor's
// WebMessageListener object — so the web bundle's nativeBridge.ts captures it
// identically and is unaware of which engine it runs on.
//
// The frames are opaque JSON strings; the C++ core verifies the capability token
// carried in each inbound frame, so this relay enforces no policy of its own.

(() => {
  // Routes to the embedder's WebExtension.MessageDelegate registered for "browser".
  let port;
  try {
    port = browser.runtime.connectNative("browser");
  } catch (e) {
    return; // No native messaging available; web bundle falls back to WebSocket.
  }

  // window.wrappedJSObject is the page's (waived) global; objects created and
  // properties defined through it are visible to page scripts.
  const pageWin = window.wrappedJSObject;

  // The page's onmessage handler, captured via the setter below. We keep it in
  // this content-script closure because reading a page-set plain property back
  // through Xray vision is unreliable, whereas a setter always fires.
  let pageHandler = null;

  const bridge = new pageWin.Object();

  // postMessage: page -> native. exportFunction makes this content-script closure
  // callable from the page world.
  exportFunction(
    (data) => {
      try {
        port.postMessage(String(data));
      } catch (e) {
        // Port disconnected; drop. The page's pending RPCs will time out.
      }
    },
    bridge,
    { defineAs: "postMessage" },
  );

  // onmessage: an accessor whose setter captures the page's handler.
  const desc = new pageWin.Object();
  desc.configurable = true;
  desc.enumerable = true;
  desc.set = exportFunction((fn) => {
    pageHandler = fn;
  }, pageWin);
  desc.get = exportFunction(() => pageHandler, pageWin);
  pageWin.Object.defineProperty(bridge, "onmessage", desc);

  pageWin.__sbcNativeRpc = bridge;

  // native -> page: native wraps each outbound frame as { d: "<json>" } (the
  // GeckoView Port.postMessage on the native side accepts only a JSON object).
  port.onMessage.addListener((msg) => {
    const frame = msg && msg.d;
    if (typeof frame !== "string") return;
    if (typeof pageHandler === "function") {
      pageHandler(cloneInto({ data: frame }, pageWin));
    }
  });
})();
