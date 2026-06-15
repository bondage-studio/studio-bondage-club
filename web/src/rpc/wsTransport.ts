import {
  clearTimeoutFn,
  jsonParse,
  jsonStringify,
  PristineWebSocket,
  setTimeoutFn,
} from "@/rpc/pristine";
import { getRpcToken } from "@/rpc/token";
import type { RpcConnectedHandler, RpcEventHandler, RpcTransport } from "@/rpc/transport";

const PROTOCOL = 1;

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

interface Subscription {
  method: string;
  params: unknown;
  onEvent: RpcEventHandler;
  onConnected: RpcConnectedHandler;
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
interface WelcomeFrame {
  t: "welcome";
}
type ServerFrame = ResFrame | EventFrame | WelcomeFrame;

function rpcUrl(): string {
  const url = new URL(window.location.origin);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  url.pathname = "/rpc";
  return url.toString();
}

// createWsTransport builds the single multiplexed /rpc connection. One socket
// carries all requests/responses (correlated by id) and all subscription
// streams. It reconnects with capped exponential backoff and re-issues active
// subscriptions on every (re)connect.
export function createWsTransport(): RpcTransport {
  let ws: WebSocket | null = null;
  let ready = false;
  let nextId = 1;
  let backoff = 1000;
  let reconnectTimer: ReturnType<typeof setTimeoutFn> | null = null;

  const pending = new Map<number, PendingCall>();
  const subs = new Map<number, Subscription>();
  // Request frames issued before the handshake completes wait here.
  let outbox: string[] = [];

  function send(frame: unknown) {
    const text = jsonStringify(frame);
    if (ws && ready && ws.readyState === PristineWebSocket.OPEN) {
      ws.send(text);
    } else {
      outbox.push(text);
      connect();
    }
  }

  function flushOutbox() {
    if (!ws || ws.readyState !== PristineWebSocket.OPEN) return;
    const queued = outbox;
    outbox = [];
    for (const text of queued) ws.send(text);
  }

  function notifyConnected(connected: boolean) {
    for (const sub of subs.values()) sub.onConnected(connected);
  }

  function handleFrame(frame: ServerFrame) {
    if (frame.t === "welcome") {
      ready = true;
      backoff = 1000;
      // Re-arm every active subscription, then release queued requests.
      for (const [id, sub] of subs) {
        ws?.send(jsonStringify({ t: "sub", id, method: sub.method, params: sub.params }));
      }
      flushOutbox();
      notifyConnected(true);
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
      subs.get(frame.id)?.onEvent(frame.data);
    }
  }

  function connect() {
    if (
      ws &&
      (ws.readyState === PristineWebSocket.OPEN || ws.readyState === PristineWebSocket.CONNECTING)
    ) {
      return;
    }
    const socket = new PristineWebSocket(rpcUrl());
    ws = socket;

    socket.addEventListener("open", () => {
      // The capability handshake: the backend verifies the token before any
      // request/subscription is honoured. ready flips on the welcome frame.
      socket.send(jsonStringify({ t: "hello", protocol: PROTOCOL, token: getRpcToken() }));
    });

    socket.addEventListener("message", (e) => {
      let frame: ServerFrame;
      try {
        frame = jsonParse(e.data as string) as ServerFrame;
      } catch {
        return;
      }
      handleFrame(frame);
    });

    const onDown = () => {
      if (ws !== socket) return; // a newer socket already supersedes this one
      ws = null;
      ready = false;
      notifyConnected(false);
      // Fail in-flight calls so awaiting callers don't hang; subscriptions
      // persist and are re-armed on reconnect.
      for (const call of pending.values()) call.reject(new Error("RPC connection closed"));
      pending.clear();
      // Reconnect only while there is something to serve.
      if (reconnectTimer) clearTimeoutFn(reconnectTimer);
      reconnectTimer = setTimeoutFn(() => {
        backoff = Math.min(backoff * 2, 30_000);
        if (subs.size > 0 || outbox.length > 0) connect();
      }, backoff);
    };
    socket.addEventListener("close", onDown);
    socket.addEventListener("error", () => socket.close());
  }

  return {
    call(method, params) {
      return new Promise<unknown>((resolve, reject) => {
        const id = nextId++;
        pending.set(id, { resolve, reject });
        send({ t: "req", id, method, params: params ?? {} });
      });
    },
    subscribe(method, params, onEvent, onConnected) {
      const id = nextId++;
      subs.set(id, { method, params, onEvent, onConnected });
      if (ready) {
        send({ t: "sub", id, method, params: params ?? {} });
        onConnected(true);
      } else {
        connect();
      }
      return () => {
        subs.delete(id);
        if (ws && ready && ws.readyState === PristineWebSocket.OPEN) {
          ws.send(jsonStringify({ t: "unsub", id }));
        }
      };
    },
  };
}
