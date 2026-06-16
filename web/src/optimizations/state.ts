// Trigger state machine. Watches input idleness and tab visibility/focus, walks
// the configured rules (first match wins) to pick a profile, and hands that
// profile's feature set to the registry (which drives the live flags). Re-evaluated
// on input/visibility/focus events and a 1s tick (to cross idle thresholds).

import type { OptimizationFeatures, OptimizationSettings } from "@/types";

import { dbg } from "./debug";
import { applyFeatures } from "./registry";

const ALL_OFF: OptimizationFeatures = {
  lazyCanvas: false,
  idleFpsThrottle: false,
  skipValidation: false,
  chatLogTrim: false,
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

function resolveProfileId(s: OptimizationSettings): string | null {
  for (const rule of s.rules) {
    switch (rule.trigger) {
      case "default":
        return rule.profile;
      case "background":
        if (isBackground()) return rule.profile;
        break;
      case "idle": {
        const ms = (rule.idleSeconds ?? 30) * 1000;
        if (Date.now() - lastInputAt >= ms) return rule.profile;
        break;
      }
    }
  }
  return null;
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
