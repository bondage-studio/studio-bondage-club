// tickRecorder — time each chat-room character pass into a 5-minute ring buffer
// that window.tps() reads (see tps.ts). Priority 20 sits between the idle-skip (30)
// and the lazy-canvas flag (10) on ChatRoomCharacterViewDraw, so it measures the
// real draw whenever one actually happens.

import { flags } from "../flags";
import type { Optimization } from "../optimization";

export interface TickRecord {
  time: number;
  duration: number;
}

const tickRecords: TickRecord[] = [];

export function getTickRecords(): readonly TickRecord[] {
  return tickRecords;
}

export const tickRecorder: Optimization = {
  key: "tickRecorder",
  install(mod) {
    mod.hookFunction("ChatRoomCharacterViewDraw", 20, (args, next) => {
      if (!flags.tickRecorder) return next(args);
      const t0 = performance.now();
      try {
        return next(args);
      } finally {
        tickRecords.push({ time: Date.now(), duration: performance.now() - t0 });
      }
    });

    // Drop samples older than ~5 minutes.
    setInterval(() => {
      const cutoff = Date.now() - 305000;
      while (tickRecords.length > 0 && tickRecords[0].time < cutoff) tickRecords.shift();
    }, 5000);
  },
};
