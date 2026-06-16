import { createNativeTransport, hasNativeBridge } from "@/rpc/nativeTransport";
import type { RpcConnectedHandler, RpcEventHandler, RpcTransport } from "@/rpc/transport";
import { createWsTransport } from "@/rpc/wsTransport";

// rpcClient is the single capability-gated channel to the C++ backend. The
// transport is chosen once and created lazily on first use, so importing this
// module has no side effects. A native host (the Android WebView or the desktop
// CEF window) injects window.__sbcNativeRpc, captured by nativeBridge.ts; when
// present we drive RPC straight over that bridge, otherwise (a plain browser) we
// fall back to the multiplexed localhost WebSocket.
let transport: RpcTransport | null = null;
function getTransport(): RpcTransport {
  if (!transport) {
    transport = hasNativeBridge() ? createNativeTransport() : createWsTransport();
  }
  return transport;
}

export const rpcClient = {
  call<T>(method: string, params: unknown = {}): Promise<T> {
    return getTransport().call(method, params) as Promise<T>;
  },
  subscribe(
    method: string,
    params: unknown,
    onEvent: RpcEventHandler,
    onConnected: RpcConnectedHandler,
  ): () => void {
    return getTransport().subscribe(method, params, onEvent, onConnected);
  },
};
