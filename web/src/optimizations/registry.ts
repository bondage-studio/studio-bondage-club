import type { OptimizationFeatures } from "@/types";

import { chatLogTrim } from "./features/chatLogTrim";
import { idleFpsThrottle } from "./features/idleFpsThrottle";
import { installFrameRecorder } from "./features/frameRecorder";
import { lazyCanvas } from "./features/lazyCanvas";
import { installRenderTracker } from "./features/renderTracker";
import { skipValidation } from "./features/skipValidation";
import { tintCache } from "./features/tintCache";
import { dbg } from "./debug";
import { flags } from "./flags";
import type { Optimization } from "./optimization";

const OPTIMIZATIONS: Optimization[] = [
  lazyCanvas,
  idleFpsThrottle,
  skipValidation,
  chatLogTrim,
  tintCache,
];

let installed = false;

// Resolves once the game function the hooks patch exists. ChatRoomCharacterViewDraw
// is defined late in BC's load sequence, so its presence implies the rest are ready.
function whenGameReady(): Promise<void> {
  if (typeof ChatRoomCharacterViewDraw === "function") return Promise.resolve();
  dbg("game not ready; polling for ChatRoomCharacterViewDraw");
  let polls = 0;
  return new Promise((resolve) => {
    const timer = setInterval(() => {
      polls++;
      if (typeof ChatRoomCharacterViewDraw === "function") {
        clearInterval(timer);
        dbg(`game ready after ${polls} polls (~${polls * 100}ms)`);
        resolve();
      }
    }, 100);
  });
}

/** Install every optimization's hooks once BC is ready. Idempotent. */
export async function installHooks(mod: ModSDKModAPI): Promise<void> {
  if (installed) {
    dbg("installHooks called again; already installed");
    return;
  }
  installed = true;
  await whenGameReady();
  for (const opt of OPTIMIZATIONS) {
    opt.install(mod);
    dbg(`installed hook: ${opt.key}`);
  }
  // Always-on telemetry — not a configurable feature, so installed directly.
  installFrameRecorder(mod);
  installRenderTracker(mod);
  dbg(`all ${OPTIMIZATIONS.length} optimizations installed (+ frame recorder, render tracker)`);
}

/**
 * Apply a profile's feature set to the live flags. Flags are set first, then any
 * feature that just turned off gets its onDisabled() (e.g. lazyCanvas repaints the
 * characters it had frozen) — run with the flag already false so the hook is a
 * pass-through during cleanup.
 */
export function applyFeatures(features: OptimizationFeatures): void {
  for (const opt of OPTIMIZATIONS) {
    const next = features[opt.key];
    const was = flags[opt.key];
    flags[opt.key] = next;
    if (was !== next) dbg(`flag ${opt.key}: ${was} -> ${next}`);
    if (was && !next) opt.onDisabled?.();
  }
}
