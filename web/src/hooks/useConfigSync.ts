import { useEffect } from "react";
import { rpcClient } from "@/rpc/client";
import type { ConfigEvent } from "@/types";

// Live config feed. Backed by the RPC `config.subscribe` event stream: the first
// frame is a full snapshot, then one frame per applied change (from any client,
// the desktop host, or a direct file edit). The transport re-subscribes on
// reconnect, which re-delivers a fresh snapshot. `onEvent` must be stable
// (wrap it in useCallback) so the subscription isn't torn down each render.
export function useConfigSync(onEvent: (ev: ConfigEvent) => void): void {
  useEffect(() => {
    return rpcClient.subscribe(
      "config.subscribe",
      {},
      (data) => onEvent(data as ConfigEvent),
      () => {},
    );
  }, [onEvent]);
}
