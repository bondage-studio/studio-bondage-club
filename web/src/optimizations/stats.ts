// Snapshot of the live optimization stats, computed from the tickRecorder ring
// buffer and the lazyCanvas per-character state. Pure read-only aggregation —
// the OptimizationsTab polls collectPerfStats() once a second to drive its charts
// (this replaced the old window.tps() console dump).

import { getCharacterStates } from "./features/lazyCanvas";
import { getTickRecords } from "./features/tickRecorder";

/** Aggregate tick timings over a trailing window (e.g. last 5s). */
export interface PerfWindow {
  label: string;
  seconds: number;
  /** Ticks per second across the window. */
  tps: number;
  /** Mean / 90th-percentile / max milliseconds-per-tick. */
  avg: number;
  p90: number;
  max: number;
  count: number;
}

/** One per-second bucket for the live time-series charts. */
export interface PerfSample {
  /** Whole seconds before "now" (0 = the current second, 59 = oldest). */
  agoSec: number;
  tps: number;
  mspt: number;
}

/** Draw vs. skip accounting for a single tracked character. */
export interface CharStat {
  /** CharacterID — the map key, used only as a stable React key. */
  id: string;
  /** Human label: "nickname (username) id", or "(username) id" with no nickname. */
  label: string;
  drawTimes: number;
  skipTimes: number;
  /** Fraction of build calls skipped, 0–1 (the lazyCanvas payoff). */
  skipRatio: number;
  avgDraw: number;
  lastSeenSec: number;
}

export interface PerfStats {
  windows: PerfWindow[];
  /** Oldest → newest, one entry per second over the trailing SERIES_SECONDS. */
  series: PerfSample[];
  characters: CharStat[];
  hasData: boolean;
}

const WINDOWS: { label: string; seconds: number }[] = [
  { label: "1s", seconds: 1 },
  { label: "5s", seconds: 5 },
  { label: "30s", seconds: 30 },
  { label: "5min", seconds: 300 },
];

const SERIES_SECONDS = 60;

function describeCharacter(C: Character | undefined, charId: string): string {
  if (!C) return charId;
  const username = C.Name?.trim() || "?";
  const id = C.MemberNumber ?? charId;
  const nickname = C.Nickname?.trim();
  return nickname ? `${nickname} (${username}) ${id}` : `(${username}) ${id}`;
}

function percentile(sorted: number[], q: number): number {
  if (sorted.length === 0) return 0;
  let idx = Math.floor(sorted.length * q);
  if (idx >= sorted.length) idx = sorted.length - 1;
  return sorted[idx];
}

export function collectPerfStats(): PerfStats {
  const now = Date.now();
  const ticks = getTickRecords();

  const windows: PerfWindow[] = WINDOWS.map(({ label, seconds }) => {
    const cutoff = now - seconds * 1000;
    const durations = ticks
      .filter((r) => r.time >= cutoff)
      .map((r) => r.duration)
      .sort((a, b) => a - b);
    const count = durations.length;
    const sum = durations.reduce((a, b) => a + b, 0);
    return {
      label,
      seconds,
      count,
      tps: count / seconds,
      avg: count ? sum / count : 0,
      p90: percentile(durations, 0.9),
      max: count ? durations[count - 1] : 0,
    };
  });

  // Bucket the trailing minute into per-second slots: tps = ticks in the second,
  // mspt = their mean duration.
  const counts = new Array<number>(SERIES_SECONDS).fill(0);
  const sums = new Array<number>(SERIES_SECONDS).fill(0);
  for (const r of ticks) {
    const ago = Math.floor((now - r.time) / 1000);
    if (ago >= 0 && ago < SERIES_SECONDS) {
      counts[ago]++;
      sums[ago] += r.duration;
    }
  }
  const series: PerfSample[] = [];
  for (let ago = SERIES_SECONDS - 1; ago >= 0; ago--) {
    series.push({
      agoSec: ago,
      tps: counts[ago],
      mspt: counts[ago] ? sums[ago] / counts[ago] : 0,
    });
  }

  const states = getCharacterStates();
  const roster = typeof Character !== "undefined" ? Character : [];
  const characters: CharStat[] = [];
  for (const [id, info] of states) {
    const total = info.drawTimes + info.skipTimes;
    const charObj = roster.find((c) => c.CharacterID === id);
    characters.push({
      id,
      label: describeCharacter(charObj, id),
      drawTimes: info.drawTimes,
      skipTimes: info.skipTimes,
      skipRatio: total ? info.skipTimes / total : 0,
      avgDraw: info.drawPerformanceAvg,
      lastSeenSec: Math.round((now - info.lastSeen) / 1000),
    });
  }
  characters.sort((a, b) => b.drawTimes + b.skipTimes - (a.drawTimes + a.skipTimes));

  return {
    windows,
    series,
    characters,
    hasData: ticks.length > 0 || characters.length > 0,
  };
}