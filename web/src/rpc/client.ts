import { IS_ANDROID_BUILD } from "@/lib/platform";
import { createNativeTransport, hasNativeBridge } from "@/rpc/nativeTransport";
import type { RpcConnectedHandler, RpcEventHandler, RpcTransport } from "@/rpc/transport";
import { createWsTransport } from "@/rpc/wsTransport";

// rpcClient is the single capability-gated channel to the C++ backend. The
// transport is chosen once (native JNI bridge on Android when present, else the
// multiplexed WebSocket) and created lazily on first use, so importing this
// module has no side effects.
let transport: RpcTransport | null = null;
function getTransport(): RpcTransport {
  if (!transport) {
    transport =
      IS_ANDROID_BUILD && hasNativeBridge() ? createNativeTransport() : createWsTransport();
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
