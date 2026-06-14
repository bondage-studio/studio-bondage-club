import { useEffect, useRef, useState } from "react";
import type { StatsEvent } from "../types";

export interface SSEState {
  stats: StatsEvent | null;
  connected: boolean;
}

export function useSSE(): SSEState {
  const [stats, setStats] = useState<StatsEvent | null>(null);
  const [connected, setConnected] = useState(false);
  const backoffRef = useRef(1000);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const esRef = useRef<EventSource | null>(null);

  useEffect(() => {
    let stopped = false;

    function connect() {
      if (stopped) return;
      const es = new EventSource("/api/events");
      esRef.current = es;

      es.addEventListener("open", () => {
        setConnected(true);
        backoffRef.current = 1000;
      });

      es.addEventListener("message", (e) => {
        try {
          const data = JSON.parse(e.data as string) as StatsEvent;
          setStats(data);
        } catch {
          // ignore malformed events
        }
      });

      es.addEventListener("error", () => {
        setConnected(false);
        es.close();
        if (!stopped) {
          timerRef.current = setTimeout(() => {
            backoffRef.current = Math.min(backoffRef.current * 2, 30_000);
            connect();
          }, backoffRef.current);
        }
      });
    }

    connect();

    return () => {
      stopped = true;
      esRef.current?.close();
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, []);

  return { stats, connected };
}
