import { getFrameSamples } from "./features/frameRecorder";
import { getTrackedCharacters } from "./features/renderTracker";

/** Aggregate frame timings over a trailing window (e.g. last 5s). */
export interface PerfWindow {
  label: string;
  seconds: number;
  /** Frames per second across the window. */
  fps: number;
  /** Mean / 90th-percentile / max milliseconds-per-frame. */
  avg: number;
  p90: number;
  max: number;
  count: number;
}

/** One per-second bucket for the live time-series charts. */
export interface PerfSample {
  /** Whole seconds before "now" (0 = the current second, 59 = oldest). */
  agoSec: number;
  fps: number;
  frameMs: number;
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
  /** Mean ms to rebuild this character's canvas once — its intrinsic render cost. */
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

function describeCharacter(C: Character): string {
  const username = C.Name?.trim() || "?";
  const id = C.MemberNumber ?? C.CharacterID;
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
  const frames = getFrameSamples();

  const windows: PerfWindow[] = WINDOWS.map(({ label, seconds }) => {
    const cutoff = now - seconds * 1000;
    const durations = frames
      .filter((r) => r.time >= cutoff)
      .map((r) => r.duration)
      .sort((a, b) => a - b);
    const count = durations.length;
    const sum = durations.reduce((a, b) => a + b, 0);
    return {
      label,
      seconds,
      count,
      fps: count / seconds,
      avg: count ? sum / count : 0,
      p90: percentile(durations, 0.9),
      max: count ? durations[count - 1] : 0,
    };
  });

  // Bucket the trailing minute into per-second slots: fps = frames in the second,
  // frameMs = their mean duration.
  const counts = new Array<number>(SERIES_SECONDS).fill(0);
  const sums = new Array<number>(SERIES_SECONDS).fill(0);
  for (const r of frames) {
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
      fps: counts[ago],
      frameMs: counts[ago] ? sums[ago] / counts[ago] : 0,
    });
  }

  const characters: CharStat[] = [];
  for (const { C, stat } of getTrackedCharacters()) {
    const total = stat.rebuilds + stat.skips;
    characters.push({
      id: C.CharacterID,
      label: describeCharacter(C),
      drawTimes: stat.rebuilds,
      skipTimes: stat.skips,
      skipRatio: total ? stat.skips / total : 0,
      avgDraw: stat.rebuilds ? stat.totalMs / stat.rebuilds : 0,
      lastSeenSec: stat.lastRebuildAt ? Math.round((now - stat.lastRebuildAt) / 1000) : 0,
    });
  }
  // Heaviest renderers first — by per-rebuild cost (the intrinsic render weight).
  characters.sort((a, b) => b.avgDraw - a.avgDraw);

  return {
    windows,
    series,
    characters,
    hasData: frames.length > 0 || characters.length > 0,
  };
}