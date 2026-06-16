export interface TickRecord {
  time: number;
  duration: number;
}

const tickRecords: TickRecord[] = [];

export function getTickRecords(): readonly TickRecord[] {
  return tickRecords;
}

export function installTickRecorder(mod: ModSDKModAPI): void {
  mod.hookFunction("ChatRoomCharacterViewDraw", 20, (args, next) => {
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
}