import type { OptimizationFeatureKey } from "@/types";

// One self-contained optimization. It installs its own SDK hooks — each gated on
// its flag so they pass through when disabled — and may clean up when toggled off.
// Where several optimizations hook the same BC function, they coexist as separate
// SDK hooks ordered by priority (see registry.ts).
export interface Optimization {
  readonly key: OptimizationFeatureKey;
  install(mod: ModSDKModAPI): void;
  /** Fired when this feature transitions enabled -> disabled (live toggle). */
  onDisabled?(): void;
}
