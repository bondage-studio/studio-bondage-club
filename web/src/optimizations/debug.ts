export function dbg(...args: unknown[]): void {
  if (window.__sodiumDebug === false) return;
  console.debug("[Sodium+ debug]", ...args);
}