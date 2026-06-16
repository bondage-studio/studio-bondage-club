// The set of optimizations and the two operations the loader needs: install every
// module's hooks once, and push a profile's feature set onto the live flags.

import type { OptimizationFeatures } from "@/types";

import { chatLogTrim } from "./features/chatLogTrim";
import { idleFpsThrottle } from "./features/idleFpsThrottle";
import { lazyCanvas } from "./features/lazyCanvas";
import { skipValidation } from "./features/skipValidation";
import { tickRecorder } from "./features/tickRecorder";
import { flags } from "./flags";
import type { Optimization } from "./optimization";
import { installDiagnostics } from "./tps";

const OPTIMIZATIONS: Optimization[] = [
  lazyCanvas,
  idleFpsThrottle,
  skipValidation,
  chatLogTrim,
  tickRecorder,
];

let installed = false;

/** Install every optimization's hooks plus the tps() console command. Idempotent. */
export function installHooks(mod: ModSDKModAPI): void {
  if (installed) return;
  installed = true;
  for (const opt of OPTIMIZATIONS) opt.install(mod);
  installDiagnostics();
}

/**
 * Apply a profile's feature set to the live flags. Flags are set first, then any
 * feature that just turned off gets its onDisabled() (e.g. lazyCanvas repaints the
 * characters it had frozen) — run with the flag already false so the hook is a
 * pass-through during cleanup.
 */
export function applyFeatures(features: OptimizationFeatures): void {
  for (const opt of OPTIMIZATIONS) {
    const next = !!features[opt.key];
    const was = flags[opt.key];
    flags[opt.key] = next;
    if (was && !next) opt.onDisabled?.();
  }
}
