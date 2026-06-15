import { useEffect, useState } from "react";
import { rpcClient } from "@/rpc/client";
import type { StatsEvent } from "@/types";

export interface SSEState {
  stats: StatsEvent | null;
  connected: boolean;
}

// Live cache-stats feed. Backed by the RPC `stats.subscribe` stream (the
// transport handles reconnect + re-subscription); the name and shape are kept
// for the existing consumers. The hook name is historical — it is no longer SSE.
export function useSSE(): SSEState {
  const [stats, setStats] = useState<StatsEvent | null>(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    const unsubscribe = rpcClient.subscribe(
      "stats.subscribe",
      {},
      (data) => setStats(data as StatsEvent),
      (isConnected) => setConnected(isConnected),
    );
    return unsubscribe;
  }, []);

  return { stats, connected };
}
