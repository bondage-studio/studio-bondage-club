import { getNativeBridge } from "@/rpc/nativeBridge";
import { jsonParse, jsonStringify } from "@/rpc/pristine";
import { getRpcToken } from "@/rpc/token";
import type { RpcEventHandler, RpcTransport } from "@/rpc/transport";

// Native bridge transport (Android). It forwards RPC straight to the C++ core
// over the WebView host bridge (system: WebMessageListener; gecko: WebExtension
// port), skipping the localhost WebSocket hop. The frame protocol mirrors the WS
// transport's req/res/sub/event/unsub, with one difference: there is no
// hello/welcome handshake — the channel is a single shared, globally-visible
// object, so every outbound frame carries the capability token, which the
// backend verifies per frame (see EmbeddedServer::deliver_rpc_frame).

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

interface ResFrame {
  t: "res";
  id: number;
  ok: boolean;
  result?: unknown;
  error?: { code?: string; message?: string };
}
interface EventFrame {
  t: "event";
  id: number;
  data: unknown;
}
type ServerFrame = ResFrame | EventFrame;

export function hasNativeBridge(): boolean {
  return getNativeBridge() !== null;
}

export function createNativeTransport(): RpcTransport {
  const bridge = getNativeBridge();
  if (!bridge) {
    throw new Error("native RPC bridge is not available in this build");
  }

  let nextId = 1;
  const pending = new Map<number, PendingCall>();
  const subs = new Map<number, RpcEventHandler>();

  bridge.onmessage = (event) => {
    let frame: ServerFrame;
    try {
      frame = jsonParse(event.data) as ServerFrame;
    } catch {
      return;
    }
    if (frame.t === "res") {
      const call = pending.get(frame.id);
      if (!call) return;
      pending.delete(frame.id);
      if (frame.ok) {
        call.resolve(frame.result ?? null);
      } else {
        call.reject(new Error(frame.error?.message ?? frame.error?.code ?? "RPC error"));
      }
      return;
    }
    if (frame.t === "event") {
      subs.get(frame.id)?.(frame.data);
    }
  };

  // The token is attached here (not by callers) and held only in module closure.
  function post(frame: Record<string, unknown>) {
    bridge!.postMessage(jsonStringify({ ...frame, token: getRpcToken() }));
  }

  return {
    call(method, params) {
      return new Promise<unknown>((resolve, reject) => {
        const id = nextId++;
        pending.set(id, { resolve, reject });
        post({ t: "req", id, method, params: params ?? {} });
      });
    },
    subscribe(method, params, onEvent, onConnected) {
      const id = nextId++;
      subs.set(id, onEvent);
      post({ t: "sub", id, method, params: params ?? {} });
      // The in-process bridge is always connected (no socket to drop).
      onConnected(true);
      return () => {
        subs.delete(id);
        post({ t: "unsub", id });
      };
    },
  };
}
