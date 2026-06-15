// RpcTransport is the swappable carrier under the rpcClient. The web build uses
// a multiplexed WebSocket (wsTransport); Android can later drop in a native JNI
// bridge (nativeTransport) with the same contract and zero caller changes.

export type RpcEventHandler = (data: unknown) => void;
export type RpcConnectedHandler = (connected: boolean) => void;

export interface RpcTransport {
  // call issues a unary request and resolves with its result (or rejects with an
  // Error carrying the backend's error message).
  call(method: string, params: unknown): Promise<unknown>;

  // subscribe starts a server-push stream. onEvent fires per pushed payload;
  // onConnected reports transport connectivity changes. Returns an unsubscribe.
  subscribe(
    method: string,
    params: unknown,
    onEvent: RpcEventHandler,
    onConnected: RpcConnectedHandler,
  ): () => void;
}
