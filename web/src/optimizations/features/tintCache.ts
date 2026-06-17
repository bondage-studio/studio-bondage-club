import { dbg } from "../debug";
import { flags } from "../flags";
import type { Optimization } from "../optimization";
import { getRenderStat } from "./renderTracker";

interface TintEntry {
  // renderTracker rebuild count of the character when this was generated.
  version: number;
  // Player blind/tint signature when this was generated.
  sig: string;
  canvas: HTMLCanvasElement;
}

// Keyed by the *source* canvas object (C.Canvas / C.CanvasBlink are distinct, stable
// objects, so the two blink variants get separate entries and GC with the character).
let tintEntries = new WeakMap<HTMLCanvasElement, TintEntry>();

// Off-path scratch, mirroring BC's shared TempCanvas, so a disabled feature still
// allocates nothing per frame.
let scratch: CanvasRenderingContext2D | null = null;
function scratchCtx(): CanvasRenderingContext2D {
  if (!scratch) scratch = document.createElement("canvas").getContext("2d");
  return scratch as CanvasRenderingContext2D;
}

// Blind/tint signature, recomputed once per frame (every character in a frame shares
// the same player state). CurrentTime is BC's per-frame timestamp.
let sigFrame = -1;
let sigValue = "";
function tintSignature(): string {
  if (CurrentTime === sigFrame) return sigValue;
  sigFrame = CurrentTime;
  let s = "";
  if (Player.IsBlind()) s = "b" + Math.min(CharacterGetDarkFactor(Player) * 2, 1);
  for (const t of Player.GetTints()) s += `|${t.r},${t.g},${t.b},${t.a}`;
  sigValue = s;
  return s;
}

// Faithful port of DrawCharacter's overlay onto the given context's canvas.
function paintTint(src: HTMLCanvasElement, ctx: CanvasRenderingContext2D): HTMLCanvasElement {
  ctx.canvas.width = CanvasDrawWidth;
  ctx.canvas.height = CanvasDrawHeight;
  ctx.globalCompositeOperation = "copy";
  ctx.drawImage(src, 0, 0);
  ctx.globalCompositeOperation = "source-atop";
  if (Player.IsBlind()) {
    const DarkFactor = Math.min(CharacterGetDarkFactor(Player) * 2, 1);
    ctx.fillStyle = `rgba(0,0,0,${1.0 - DarkFactor})`;
    ctx.fillRect(0, 0, src.width, src.height);
  }
  for (const { r, g, b, a } of Player.GetTints()) {
    ctx.fillStyle = `rgba(${r},${g},${b},${a})`;
    ctx.fillRect(0, 0, src.width, src.height);
  }
  return ctx.canvas;
}

// Global entry point invoked by the patched DrawCharacter. Returns the canvas to draw:
// the original when no overlay applies, otherwise the tinted (cached) one. Mirrors
// BC's own gate so the patched block is behaviour-preserving.
function drawTint(C: Character, Canvas: HTMLCanvasElement, OverrideDark: boolean): HTMLCanvasElement {
  if (C.IsPlayer() || OverrideDark || !(Player.IsBlind() || Player.HasTints())) return Canvas;

  // Disabled: behave exactly like BC — uncached, shared scratch.
  if (!flags.tintCache) return paintTint(Canvas, scratchCtx());

  const stat = getRenderStat(C);
  const version = stat ? stat.rebuilds : 0;
  const sig = tintSignature();

  const entry = tintEntries.get(Canvas);
  if (entry && entry.version === version && entry.sig === sig) return entry.canvas;

  // Miss: reuse the entry's canvas if we have one (avoids reallocation), else create.
  const target = entry?.canvas ?? document.createElement("canvas");
  const ctx = target.getContext("2d");
  if (!ctx) return paintTint(Canvas, scratchCtx());
  paintTint(Canvas, ctx);
  tintEntries.set(Canvas, { version, sig, canvas: target });
  return target;
}

// The patched block calls this by name in global scope.
const GLOBAL_ENTRY = "SodiumOptDrawTint";
// Single-line anchor: the unique tint condition in DrawCharacter. Matched without
// leading whitespace so indentation differences don't break it. The replacement
// routes the block to our helper and neutralizes the original body via `if (false)`.
const PATCH_KEY =
  "if (!C.IsPlayer() && !OverrideDark && (Player.IsBlind() || Player.HasTints())) {";
const PATCH_VALUE = `Canvas = ${GLOBAL_ENTRY}(C, Canvas, OverrideDark); if (false) {`;

export const tintCache: Optimization = {
  key: "tintCache",
  install(mod) {
    (globalThis as Record<string, unknown>)[GLOBAL_ENTRY] = drawTint;
    mod.patchFunction("DrawCharacter", { [PATCH_KEY]: PATCH_VALUE });
    const applied =
      typeof DrawCharacter === "function" && DrawCharacter.toString().includes(GLOBAL_ENTRY);
    dbg(`tintCache: DrawCharacter patch ${applied ? "applied" : "NOT applied (pattern miss)"}`);
  },
  onDisabled() {
    // Drop cached canvases so a disabled feature doesn't pin memory. The patch stays
    // (drawTint falls back to BC-equivalent uncached behaviour while the flag is off).
    tintEntries = new WeakMap<HTMLCanvasElement, TintEntry>();
  },
};
