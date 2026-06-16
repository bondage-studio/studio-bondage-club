// window.tps(): dump TPS/MSPT and per-character draw stats to the console. Always
// available once installed; it just reports whatever the (optionally enabled)
// tickRecorder and lazyCanvas modules have collected.

import { getCharacterStates } from "./features/lazyCanvas";
import { getTickRecords } from "./features/tickRecorder";

export function installDiagnostics(): void {
  window.tps = () => {
    const log = (...a: unknown[]) => console.log("[Sodium+]", ...a);
    const now = Date.now();

    log("=== Performance Stats ===");
    const tickRecords = getTickRecords();
    [1, 5, 30, 300].forEach((sec) => {
      const cutoff = now - sec * 1000;
      const records = tickRecords.filter((r) => r.time >= cutoff);
      const label = sec === 300 ? "5min" : `${sec}s`;
      if (records.length === 0) {
        log(`[${label}] No data`);
        return;
      }
      const tps = (records.length / sec).toFixed(2);
      const durations = records.map((r) => r.duration).sort((a, b) => a - b);
      const max = durations[durations.length - 1].toFixed(2);
      const avg = (durations.reduce((a, b) => a + b, 0) / durations.length).toFixed(2);
      let p9idx = Math.floor(durations.length * 0.9);
      if (p9idx >= durations.length) p9idx = durations.length - 1;
      const p9 = durations[p9idx].toFixed(2);
      log(
        `[${label.padEnd(4, " ")}] TPS: ${tps.padStart(5, " ")} | ` +
          `MSPT (avg/90%/max): ${avg.padStart(6, " ")} / ${p9.padStart(6, " ")} / ${max.padStart(6, " ")}`,
      );
    });

    log("=== Character Stats ===");
    const characterStates = getCharacterStates();
    if (characterStates.size === 0) {
      log("(No character data)");
      return;
    }
    log(`Total characters tracked: ${characterStates.size}`);
    for (const [id, info] of characterStates) {
      const charObj = window.Character?.find((w) => w.CharacterID === id);
      const nickname = charObj?.Nickname ? ` | nickname: ${charObj.Nickname}` : "";
      log(
        `  Character #${id} | drawTimes: ${info.drawTimes} | skipTimes: ${info.skipTimes} | ` +
          `avgDraw: ${info.drawPerformanceAvg}ms | lastSeen: ${Math.round((now - info.lastSeen) / 1000)}s ago${nickname}`,
      );
    }
  };
}
