import { dbg } from "../debug";
import { flags } from "../flags";
import type { Optimization } from "../optimization";
import { flagSkip, getRenderStat } from "./renderTracker";

interface LazyState {
  createdAt: number;
  nextStaleDraw: number;
  nextForceDraw: number;
  lastHash: number | null;
  isDirty: boolean;
  dirtyAt: number;
}

// Keyed by the Character object: entries are GC'd with the character, so no
// manual eviction and no risk of a stale string-id key.
const lazyStates = new WeakMap<Character, LazyState>();
// True while a chat-room character pass is running, so the build hook can tell an
// in-loop redraw from an out-of-loop deferred one.
let isDrawing = false;
// Set while DrawCharacter forces a real rebuild of a stale character so the build
// hook bypasses its skip logic for that one call.
let forceBuildCanvas = false;
// One-shot guard so the build hook logs its first invocation (and the flag state
// at that moment) without spamming the hot path.
let loggedBuild = false;

const FORCE_DRAW_MS = 5000;

function fold(s: string, h: number): number {
  h = (h * 31 + s.length) | 0;
  for (let i = 0; i < s.length; i++) {
    h = (h * 31 + s.charCodeAt(i)) | 0;
  }
  return h;
}

function appearanceHash(C: Character): number {
  let h = 0;
  const app = C.Appearance;
  if (app) {
    for (let i = 0; i < app.length; i++) {
      const item = app[i];
      if (!item || !item.Asset) {
        h = fold("∅", h);
        continue;
      }
      const a = item.Asset;
      h = fold(a.Group ? a.Group.Name : "", h);
      h = fold(a.Name || "", h);
      if (item.Color != null) h = fold(JSON.stringify(item.Color), h);
      if (item.Property != null) h = fold(JSON.stringify(item.Property), h);
    }
  }
  // Resolved pose (BodyLower/BodyUpper/BodyFull/BodyHands/...): not in Appearance.
  if (C.PoseMapping) h = fold(JSON.stringify(C.PoseMapping), h);
  return h;
}

export const lazyCanvas: Optimization = {
  key: "lazyCanvas",
  install(mod) {
    // Mark the draw loop so the build hook can detect in-loop redraws (priority 10,
    // innermost on ChatRoomCharacterViewDraw).
    mod.hookFunction("ChatRoomCharacterViewDraw", 10, (args, next) => {
      if (!flags.lazyCanvas) return next(args);
      isDrawing = true;
      try {
        return next(args);
      } finally {
        isDrawing = false;
      }
    });

    mod.hookFunction("CharacterAppearanceBuildCanvas", 10, (args, next) => {
      if (!loggedBuild) {
        loggedBuild = true;
        dbg(`CharacterAppearanceBuildCanvas hook entered; lazyCanvas flag=${flags.lazyCanvas}`);
      }
      if (!flags.lazyCanvas) return next(args);
      const C = args[0] as Character | undefined;
      // Characters without an ID (appearance-editor dummy, mod probes) build normally.
      if (!C || !C.CharacterID) return undefined;
      if (forceBuildCanvas) return next(args);
      if (CurrentScreen !== "ChatRoom" || CurrentCharacter) return next(args);

      const now = Date.now();
      let state = lazyStates.get(C);
      if (!state) {
        state = {
          createdAt: now,
          nextStaleDraw: 0,
          nextForceDraw: 0,
          lastHash: null,
          isDirty: false,
          dirtyAt: 0,
        };
        lazyStates.set(C, state);
      }

      // Called outside the chat-room draw loop: defer to DrawCharacter.
      if (!isDrawing) {
        if (!state.isDirty) {
          state.dirtyAt = now;
          state.isDirty = true;
        }
        state.lastHash = null;
        flagSkip();
        return undefined;
      }

      let allowDraw = false;
      let currentHash: number | null = null;

      if (now - state.createdAt <= 15000) {
        // performance.now() is noisy in the first seconds; just draw.
        allowDraw = true;
      } else if (now > state.nextForceDraw) {
        // Heartbeat redraw guards against hash collisions leaving a stale canvas.
        allowDraw = true;
      } else {
        currentHash = appearanceHash(C);
        if (currentHash !== state.lastHash && now > state.nextStaleDraw) {
          allowDraw = true;
        }
      }

      if (!allowDraw) {
        flagSkip();
        return undefined;
      }

      // Let the paint run; the render tracker (outer hook) measures and records it.
      const res = next(args);

      state.lastHash = currentHash;
      const after = Date.now();
      state.nextForceDraw = after + FORCE_DRAW_MS;
      // Back off proportionally to how expensive this character is to draw. The
      // current rebuild is recorded by the tracker after we return, so this reads
      // the running average up to (not including) it — fine for a heuristic.
      const prior = getRenderStat(C);
      const avg = prior && prior.rebuilds ? prior.totalMs / prior.rebuilds : 0;
      state.nextStaleDraw = after + avg * 100;
      return res;
    });

    // Flush a character whose rebuild was deferred while it was off the draw loop.
    mod.hookFunction("DrawCharacter", 10, (args, next) => {
      if (flags.lazyCanvas) {
        const C = args[0] as Character | undefined;
        if (C && C.CharacterID) {
          const state = lazyStates.get(C);
          if (state && state.isDirty && Date.now() - state.dirtyAt > 1000) {
            forceBuildCanvas = true;
            try {
              CharacterAppearanceBuildCanvas(C);
            } finally {
              forceBuildCanvas = false;
            }
            state.isDirty = false;
          }
        }
      }
      return next(args);
    });
  },
  onDisabled() {
    // Hand control back to BC: flag every loaded character dirty so its next
    // DrawCharacter rebuilds the canvas we may have left stale (MustDraw is exactly
    // the engine's own "needs a rebuild" signal). Our WeakMap state is dropped with
    // the characters; no manual teardown needed.
    if (typeof Character !== "undefined") {
      for (const C of Character) C.MustDraw = true;
    }
  },
};