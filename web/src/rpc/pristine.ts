// Pristine primitive capture (defense-in-depth).
//
// This module is imported first from the app entry (main.tsx), so it evaluates
// before any community userscript we inject can monkeypatch globals. The RPC
// transport uses these captured references rather than the live `window.*`, so a
// later patch of `window.WebSocket` (or JSON, timers, ...) cannot intercept or
// tamper with the capability channel.
//
// This is not a hard guarantee on its own — the hard boundary is the backend
// token check (see src/server/rpc/auth.hpp). It only raises the bar against
// in-realm interference from the code we load after ourselves.

export const PristineWebSocket: typeof WebSocket = window.WebSocket;

export const jsonParse = JSON.parse;
export const jsonStringify = JSON.stringify;

export const setTimeoutFn = window.setTimeout.bind(window);
export const clearTimeoutFn = window.clearTimeout.bind(window);
