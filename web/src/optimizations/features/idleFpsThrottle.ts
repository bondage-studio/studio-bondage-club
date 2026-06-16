// idleFpsThrottle — pace the chat-room redraw to ~5fps. When the full-screen black
// backdrop is painted in a quiet chat room, drop the following character draw and
// its arousal overlay. Owns the per-frame skip flag end to end.

import { flags } from "../flags";
import type { Optimization } from "../optimization";

const THROTTLE_FRAME_MS = 210;

let skipNextChatRoomDraw = false;
let lastDrawRectTime = 0;

function inOptimizableChatRoom(): boolean {
  if (CurrentScreen !== "ChatRoom" || CurrentCharacter) return false;
  return !(typeof ChatRoomIsViewActive === "function" && ChatRoomIsViewActive("Map"));
}

export const idleFpsThrottle: Optimization = {
  key: "idleFpsThrottle",
  install(mod) {
    mod.hookFunction("DrawRect", 10, (args, next) => {
      if (flags.idleFpsThrottle) {
        const [Left, Top, Width, Height, Color] = args;
        if (Left === 0 && Top === 0 && Width === 2000 && Height === 1000 && Color === "Black") {
          if (inOptimizableChatRoom()) {
            const now = Date.now();
            if (now - lastDrawRectTime < THROTTLE_FRAME_MS) {
              skipNextChatRoomDraw = true;
              return undefined;
            }
            lastDrawRectTime = now;
          }
        }
      }
      return next(args);
    });

    // Outermost ChatRoomCharacterViewDraw hook (priority 30): short-circuit a
    // throttled frame entirely, before the recorder/lazy-canvas layers.
    mod.hookFunction("ChatRoomCharacterViewDraw", 30, (args, next) => {
      if (skipNextChatRoomDraw) return undefined;
      return next(args);
    });

    // Consume the skip flag unconditionally so it can never get stuck on a toggle.
    mod.hookFunction("ChatRoomDrawArousalOverlay", 10, (args, next) => {
      if (skipNextChatRoomDraw) {
        skipNextChatRoomDraw = false;
        return false;
      }
      return next(args);
    });
  },
  onDisabled() {
    skipNextChatRoomDraw = false;
  },
};
