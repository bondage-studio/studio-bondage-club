import { dbg } from "../debug";
import { flags } from "../flags";
import type { Optimization } from "../optimization";
import { flagSkip, getRenderStat } from "./renderTracker";

interface LazyState {
  createdAt: number;
  nextStaleDraw: number;
  nextForceDraw: number;
  lastHash: number | null;
  loadHash: number | null;
  isDirty: boolean;
  dirtyAt: number;
  builtWidth: number;
}

// WeakMap so entries are GC'd with the character — no manual eviction, no stale keys.
const lazyStates = new WeakMap<Character, LazyState>();
// True only during a chat-room character pass; lets the build hook tell an in-loop
// redraw from an out-of-loop one, which it defers instead of deciding blindly.
let isDrawing = false;
// Set while we drive a forced rebuild, so the build hook lets that one call through.
let forceBuildCanvas = false;
let loggedBuild = false;

const FORCE_DRAW_MS = 5000;

// Draw-geometry guard for echo-clothing-ext, which doubles every character canvas
// (500->1000) and bakes a body offset re-applied in its patched DrawCharacter. Reusing a
// canvas built in the other mode stretches/shifts it ("flying clothes"), so it must be
// rebuilt. Why builtWidth and not canvas.width: echo's own DrawCharacter hook rewrites
// canvas.width racily, and a stale canvas hashes identically every frame — the width WE
// last painted is the only reliable signal. Expected = GLDrawCanvas.width / 2 (character
// and blink share it); nothing built yet, or no WebGL, -> not stale.
function geometryStale(state: LazyState): boolean {
  if (!GLDrawCanvas || state.builtWidth === 0) return false;
  return state.builtWidth !== GLDrawCanvas.width / 2;
}

function ensureState(C: Character, now: number): LazyState {
  let state = lazyStates.get(C);
  if (!state) {
    state = {
      createdAt: now,
      nextStaleDraw: 0,
      nextForceDraw: 0,
      lastHash: null,
      loadHash: null,
      isDirty: false,
      dirtyAt: 0,
      builtWidth: 0,
    };
    lazyStates.set(C, state);
  }
  return state;
}

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
      // Mark the draw loop so the build hook can distinguish in-loop redraws.
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
      const state = ensureState(C, now);
      // Force a rebuild on a geometry flip — in or out of the loop, never deferred.
      const stale = geometryStale(state);

      // Out of the draw loop: defer the skip/draw decision to DrawCharacter.
      if (!isDrawing && !stale) {
        if (!state.isDirty) {
          state.dirtyAt = now;
          state.isDirty = true;
        }
        state.lastHash = null;
        flagSkip();
        return undefined;
      }

      let allowDraw = stale;
      let currentHash: number | null = null;

      if (allowDraw) {
        // geometry mismatch: already forced
      } else if (now - state.createdAt <= 15000) {
        // Render timings are noisy in the first seconds, so don't trust the backoff yet.
        allowDraw = true;
      } else if (now > state.nextForceDraw) {
        // Heartbeat: bound how long a hash collision can leave a stale canvas up.
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

      const res = next(args);

      // Record the mode we built in so geometryStale can detect a later flip.
      if (C.Canvas) state.builtWidth = C.Canvas.width;
      state.isDirty = false;
      state.lastHash = currentHash;
      const after = Date.now();
      state.nextForceDraw = after + FORCE_DRAW_MS;
      // Back off longer for characters that are expensive to draw. getRenderStat reads
      // the average up to (not including) this rebuild — fine for a heuristic.
      const prior = getRenderStat(C);
      const avg = prior && prior.rebuilds ? prior.totalMs / prior.rebuilds : 0;
      state.nextStaleDraw = after + avg * 100;
      return res;
    });

    // Skip the prep, not just the paint. Animated assets retrigger CharacterRefresh ->
    // CharacterLoadCanvas every refresh, re-running the Stringify+parse deep copy,
    // SortLayers and BuildMasks even when nothing changed — the BuildCanvas gate above
    // only skips the paint. When the appearance+pose hash matches the last full load the
    // cached layers/masks are still valid, so reuse them and only repaint.
    mod.hookFunction("CharacterLoadCanvas", 10, (args, next) => {
      if (!flags.lazyCanvas) return next(args);
      const C = args[0] as Character | undefined;
      if (!C || !C.CharacterID) return next(args);
      if (forceBuildCanvas) return next(args);
      if (CurrentScreen !== "ChatRoom" || CurrentCharacter) return next(args);

      const now = Date.now();
      const state = ensureState(C, now);
      const hash = appearanceHash(C);

      // First load or a real appearance/pose change: run the full prep, record its hash.
      if (state.loadHash === null || state.loadHash !== hash) {
        const res = next(args);
        state.loadHash = hash;
        return res;
      }

      // Unchanged: reuse cached layers/masks, only repaint. BuildCanvas decides if the
      // paint runs; clearing MustDraw mirrors the real CharacterLoadCanvas tail.
      CharacterAppearanceBuildCanvas(C);
      C.MustDraw = false;
      return undefined;
    });

    mod.hookFunction("DrawCharacter", 10, (args, next) => {
      if (flags.lazyCanvas) {
        const C = args[0] as Character | undefined;
        if (C && C.CharacterID) {
          const state = lazyStates.get(C);
          if (state && geometryStale(state)) {
            // A geometry flip doesn't set MustDraw, so BC would just blit the stale
            // canvas (flying clothes) forever. Force MustDraw so the real DrawCharacter
            // routes through CharacterLoadCanvas -> BuildCanvas and rebuilds it.
            C.MustDraw = true;
          } else if (state && state.isDirty && Date.now() - state.dirtyAt > 1000) {
            // Flush a rebuild deferred while the character was off the draw loop.
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
    if (typeof Character !== "undefined") {
      for (const C of Character) C.MustDraw = true;
    }
  },
};
