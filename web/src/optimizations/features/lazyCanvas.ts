// lazyCanvas — the core optimization: skip redundant character canvas rebuilds.
// Outside the chat-room draw loop a rebuild is deferred (marked dirty) and flushed
// by DrawCharacter; inside it, a character is rebuilt only when new, on a 5s
// heartbeat, or when its appearance hash changes.

import { flags } from "../flags";
import type { Optimization } from "../optimization";

export interface CharState {
  createdAt: number;
  lastSeen: number;
  drawPerformanceSum: number;
  drawPerformanceAvg: number;
  drawTimes: number;
  skipTimes: number;
  nextStaleDraw: number;
  nextForceDraw: number;
  lastHash: number | null;
  isDirty?: boolean;
  dirtyAt?: number;
}

const characterStates = new Map<number, CharState>();
// True while a chat-room character pass is running, so the build hook can tell an
// in-loop redraw from an out-of-loop deferred one.
let isDrawing = false;
// Set while DrawCharacter forces a real rebuild of a stale character so the build
// hook bypasses its skip logic for that one call.
let forceBuildCanvas = false;

const FORCE_DRAW_MS = 5000;

export function getCharacterStates(): ReadonlyMap<number, CharState> {
  return characterStates;
}

// Cheap 16-bit rolling hash over item descriptions: a fast staleness signal.
function sodiumHash(updater: string | null | undefined, pointer: { value: number }): void {
  let h = pointer.value;
  if (!updater) {
    h = h * 31 + 17;
  } else {
    const len = updater.length;
    h = h * 31 + len;
    h = h * 31 + (updater.charCodeAt(0) || 0);
    h = h * 31 + (updater.charCodeAt(len >> 1) || 0);
    h = h * 31 + (updater.charCodeAt(len - 1) || 0);
  }
  pointer.value = h & 0xffff;
}

// Repaint and forget every tracked character; called when lazyCanvas turns off so
// canvases skipped while it was active refresh back to the live look.
function refreshTrackedCharacters(): void {
  for (const id of characterStates.keys()) {
    const C = window.Character?.find((c) => c.CharacterID === id);
    if (C) window.CharacterRefresh?.(C, false);
  }
  characterStates.clear();
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
      if (!flags.lazyCanvas) return next(args);
      const C = args[0] as BcCharacter | undefined;
      // Characters without an ID (appearance-editor dummy, mod probes) build normally.
      if (!C || !C.CharacterID) return undefined;
      if (forceBuildCanvas) return next(args);
      if (window.CurrentScreen !== "ChatRoom" || window.CurrentCharacter) return next(args);

      const now = Date.now();
      let state = characterStates.get(C.CharacterID);
      if (!state) {
        state = {
          createdAt: now,
          lastSeen: now,
          drawPerformanceSum: 0,
          drawPerformanceAvg: 0,
          drawTimes: 0,
          skipTimes: 0,
          nextStaleDraw: 0,
          nextForceDraw: 0,
          lastHash: null,
        };
        characterStates.set(C.CharacterID, state);
      }

      // Called outside the chat-room draw loop: defer to DrawCharacter.
      if (!isDrawing) {
        if (!state.isDirty) {
          state.dirtyAt = now;
          state.isDirty = true;
        }
        state.lastHash = null;
        state.skipTimes++;
        return undefined;
      }

      state.lastSeen = now;
      let allowDraw = false;
      let currentHash: number | null = null;

      if (now - state.createdAt <= 15000) {
        // performance.now() is noisy in the first seconds; just draw.
        allowDraw = true;
      } else if (now > state.nextForceDraw) {
        // Heartbeat redraw guards against hash collisions leaving a stale canvas.
        allowDraw = true;
      } else {
        const pointer = { value: 0 };
        if (C.Appearance) {
          for (let i = 0; i < C.Appearance.length; i++) {
            const item = C.Appearance[i];
            const desc = item && item.Asset ? item.Asset.Description : null;
            sodiumHash(desc, pointer);
          }
        }
        currentHash = pointer.value;
        if (currentHash !== state.lastHash && now > state.nextStaleDraw) {
          allowDraw = true;
        }
      }

      if (!allowDraw) {
        state.skipTimes++;
        return undefined;
      }

      const start = performance.now();
      const res = next(args);
      const elapsed = performance.now() - start;

      state.lastHash = currentHash;
      state.drawPerformanceSum += elapsed;
      state.drawTimes++;
      state.drawPerformanceAvg = state.drawPerformanceSum / state.drawTimes;

      const after = Date.now();
      state.nextForceDraw = after + FORCE_DRAW_MS;
      // Back off proportionally to how expensive this character is to draw.
      state.nextStaleDraw = after + state.drawPerformanceAvg * 100;
      return res;
    });

    // Flush a character whose rebuild was deferred while it was off the draw loop.
    mod.hookFunction("DrawCharacter", 10, (args, next) => {
      if (flags.lazyCanvas) {
        const C = args[0] as BcCharacter | undefined;
        if (C && C.CharacterID) {
          const state = characterStates.get(C.CharacterID);
          if (state && state.isDirty && Date.now() - (state.dirtyAt ?? 0) > 1000) {
            forceBuildCanvas = true;
            try {
              window.CharacterAppearanceBuildCanvas?.(C);
            } finally {
              forceBuildCanvas = false;
            }
            state.isDirty = false;
          }
        }
      }
      return next(args);
    });

    // Forget characters unseen for 60s.
    setInterval(() => {
      const now = Date.now();
      for (const [key, state] of characterStates) {
        if (now - state.lastSeen > 60000) characterStates.delete(key);
      }
    }, 1000);
  },
  onDisabled() {
    refreshTrackedCharacters();
  },
};
