// chatLogTrim — cap the in-DOM chat log so a long session doesn't accumulate
// thousands of nodes; keep the latest separator plus ~50 messages after it. Driven
// by an interval rather than an SDK hook.

import { flags } from "../flags";
import type { Optimization } from "../optimization";

const CHAT_LOG_KEEP = 50;

export const chatLogTrim: Optimization = {
  key: "chatLogTrim",
  install() {
    setInterval(() => {
      if (!flags.chatLogTrim) return;
      const roomSeps = document.querySelectorAll("#TextAreaChatLog .chat-room-sep");
      if (roomSeps.length === 0) return;
      const lastSep = roomSeps[roomSeps.length - 1];
      const parent = lastSep.parentElement;
      if (!parent) return;
      let currentMsgCount = 0;
      let sib = lastSep.nextSibling;
      while (sib) {
        currentMsgCount++;
        sib = sib.nextSibling;
      }
      if (currentMsgCount <= CHAT_LOG_KEEP) return;
      while (lastSep.previousSibling) parent.removeChild(lastSep.previousSibling);
      while (parent.childElementCount > CHAT_LOG_KEEP + 1) {
        if (lastSep.nextSibling) parent.removeChild(lastSep.nextSibling);
        else break;
      }
    }, 60000);
  },
};
