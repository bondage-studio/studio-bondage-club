// Native RPC bridge capture (Android).
//
// On Android the WebView host injects a bridge object at document-start — the
// system flavor via androidx.webkit WebViewCompat.addWebMessageListener, the
// gecko flavor via a built-in WebExtension content script. Both expose the same
// shape: { postMessage(string), onmessage }.
//
// Like pristine.ts, this module is imported before any community userscript we
// inject runs (see main.tsx), so it grabs the bridge into module closure and
// erases it from `window`. That keeps co-resident untrusted code from reaching
// it — but, as on the WebSocket path, the hard boundary is the per-frame
// capability token the backend verifies (see EmbeddedServer::deliver_rpc_frame).
// On non-Android builds the object is absent and getNativeBridge() returns null.

export interface NativeBridge {
  postMessage(data: string): void;
  onmessage: ((event: { data: string }) => void) | null;
}

const BRIDGE_NAME = "__sbcNativeRpc";

let bridge: NativeBridge | null = null;

const holder = window as unknown as Record<string, NativeBridge | undefined>;
const candidate = holder[BRIDGE_NAME];
if (candidate && typeof candidate.postMessage === "function") {
  bridge = candidate;
  try {
    // Best-effort: drop the global reference so later untrusted code can't find
    // it. Our closure ref (and the native channel keyed to the object identity)
    // survive; the per-frame token gates use even if this is a no-op.
    delete holder[BRIDGE_NAME];
  } catch {
    // Non-configurable on some engines — ignore.
  }
}

export function getNativeBridge(): NativeBridge | null {
  return bridge;
}
