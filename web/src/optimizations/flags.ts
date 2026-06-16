import type { OptimizationFeatures } from "@/types";

// Live feature flags, mutated by applyFeatures (registry.ts) as the state machine
// switches profiles. Every hook reads these on each call, so toggling a feature
// takes effect immediately without re-hooking; all-false is a transparent no-op.
export const flags: OptimizationFeatures = {
  lazyCanvas: false,
  idleFpsThrottle: false,
  skipValidation: false,
  chatLogTrim: false,
  tickRecorder: false,
};
