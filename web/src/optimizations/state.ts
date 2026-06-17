import type { OptimizationFeatures, OptimizationSettings, OptimizationTrigger } from "@/types";

import { dbg } from "./debug";
import { applyFeatures } from "./registry";

const ALL_OFF: OptimizationFeatures = {
  lazyCanvas: false,
  idleFpsThrottle: false,
  skipValidation: false,
  chatLogTrim: false,
  tintCache: false,
};

const INPUT_EVENTS = ["mousedown", "keydown", "mousemove", "touchstart", "wheel"] as const;

let settings: OptimizationSettings | null = null;
let lastInputAt = Date.now();
let blurred = false;
let listenersAttached = false;
// Tracks the last applied profile so we only reassign flags on a real change.
let appliedProfileId: string | null | undefined;

function markInput(): void {
  lastInputAt = Date.now();
}

function isBackground(): boolean {
  return document.visibilityState === "hidden" || blurred;
}

/** First matching rule (top to bottom), or null if none fires. */
function matchRule(s: OptimizationSettings): { profile: string; trigger: OptimizationTrigger } | null {
  for (const rule of s.rules) {
    switch (rule.trigger) {
      case "default":
        return { profile: rule.profile, trigger: "default" };
      case "background":
        if (isBackground()) return { profile: rule.profile, trigger: "background" };
        break;
      case "idle": {
        const ms = (rule.idleSeconds ?? 30) * 1000;
        if (Date.now() - lastInputAt >= ms) return { profile: rule.profile, trigger: "idle" };
        break;
      }
    }
  }
  return null;
}

function resolveProfileId(s: OptimizationSettings): string | null {
  return matchRule(s)?.profile ?? null;
}

function recompute(): void {
  if (!settings || !settings.enabled) {
    if (appliedProfileId !== "__off__") {
      appliedProfileId = "__off__";
      dbg(settings ? "config disabled (enabled=false); all features off" : "no config yet; all features off");
      applyFeatures(ALL_OFF);
    }
    return;
  }
  const id = resolveProfileId(settings);
  if (id === appliedProfileId) return;
  appliedProfileId = id;
  const profile = id ? settings.profiles.find((p) => p.id === id) : undefined;
  if (id && !profile) {
    dbg(`rule matched profile "${id}" but no such profile exists; all features off`);
  } else {
    dbg(`profile -> ${id ?? "(none)"}`, profile ? profile.features : ALL_OFF);
  }
  applyFeatures(profile ? profile.features : ALL_OFF);
}

function attachListeners(): void {
  if (listenersAttached) return;
  listenersAttached = true;
  for (const ev of INPUT_EVENTS) {
    window.addEventListener(ev, markInput, { capture: true, passive: true });
  }
  document.addEventListener("visibilitychange", recompute);
  window.addEventListener("focus", () => {
    blurred = false;
    recompute();
  });
  window.addEventListener("blur", () => {
    blurred = true;
    recompute();
  });
  window.setInterval(recompute, 1000);
}

/** Attach the input/visibility listeners and do an initial evaluation. */
export function startStateMachine(): void {
  attachListeners();
  recompute();
}

/** What the loader is doing right now, for the panel's live status readout. */
export interface OptimizationStatus {
  /** Whether a config is loaded and its master switch is on. */
  enabled: boolean;
  /** Profile currently selected by the rules, or null if none matches / disabled. */
  activeProfileId: string | null;
  /** Display name of that profile (falls back to the id), or null. */
  activeProfileName: string | null;
  /** Why that profile won: the matching rule's trigger. */
  activeTrigger: OptimizationTrigger | null;
}

export function getOptimizationStatus(): OptimizationStatus {
  if (!settings || !settings.enabled) {
    return { enabled: false, activeProfileId: null, activeProfileName: null, activeTrigger: null };
  }
  const match = matchRule(settings);
  const profile = match ? settings.profiles.find((p) => p.id === match.profile) : undefined;
  return {
    enabled: true,
    activeProfileId: match?.profile ?? null,
    activeProfileName: match ? (profile?.name ?? match.profile) : null,
    activeTrigger: match?.trigger ?? null,
  };
}

/** Set the config (initial seed or a live push from the panel) and re-evaluate. */
export function applySettings(next: OptimizationSettings): void {
  dbg("applySettings", {
    enabled: next.enabled,
    rules: next.rules?.length ?? 0,
    profiles: next.profiles?.map((p) => p.id),
  });
  settings = next;
  appliedProfileId = undefined; // force a re-apply against the new config
  recompute();
}
