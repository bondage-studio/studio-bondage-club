import { dbg } from "../debug";

export interface RenderStat {
  /** Real canvas rebuilds that ran (cost paid). */
  rebuilds: number;
  /** Rebuilds suppressed by lazyCanvas (cost avoided). */
  skips: number;
  /** Cumulative ms spent rebuilding this character. */
  totalMs: number;
  /** Duration of the most recent rebuild. */
  lastMs: number;
  /** Date.now() of the most recent rebuild, 0 if never. */
  lastRebuildAt: number;
}

const stats = new WeakMap<Character, RenderStat>();

function ensure(C: Character): RenderStat {
  let s = stats.get(C);
  if (!s) {
    s = { rebuilds: 0, skips: 0, totalMs: 0, lastMs: 0, lastRebuildAt: 0 };
    stats.set(C, s);
  }
  return s;
}

// Set by lazyCanvas (the inner hook) when it suppresses the current rebuild, so
// the outer tracker counts it as a skip rather than a zero-cost rebuild.
let pendingSkip = false;

/** Called by lazyCanvas to mark the in-flight rebuild as suppressed. */
export function flagSkip(): void {
  pendingSkip = true;
}

/** Current telemetry for a character (used by lazyCanvas for its cost-based backoff). */
export function getRenderStat(C: Character): RenderStat | undefined {
  return stats.get(C);
}

export function installRenderTracker(mod: ModSDKModAPI): void {
  mod.hookFunction("CharacterAppearanceBuildCanvas", 20, (args, next) => {
    const C = args[0] as Character | undefined;
    if (!C || !C.CharacterID) {
      return next(args);
    }
    pendingSkip = false;
    const t0 = performance.now();
    const res = next(args);
    const s = ensure(C);
    if (pendingSkip) {
      s.skips++;
    } else {
      const ms = performance.now() - t0;
      s.rebuilds++;
      s.totalMs += ms;
      s.lastMs = ms;
      s.lastRebuildAt = Date.now();
    }
    return res;
  });
  dbg("render tracker installed");
}

export interface TrackedCharacter {
  C: Character;
  stat: RenderStat;
}

/** On-screen characters (BC's last-frame draw list) that carry render telemetry. */
export function getTrackedCharacters(): TrackedCharacter[] {
  if (typeof DrawLastCharacters === "undefined") return [];
  const out: TrackedCharacter[] = [];
  const seen = new Set<Character>();
  for (const C of DrawLastCharacters) {
    if (!C || seen.has(C)) continue;
    seen.add(C);
    const stat = stats.get(C);
    if (stat) out.push({ C, stat });
  }
  return out;
}