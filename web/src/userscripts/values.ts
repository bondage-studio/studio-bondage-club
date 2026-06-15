// Per-script GM value store: a preloaded, write-through in-memory cache that
// gives Tampermonkey's synchronous GM_getValue/GM_setValue semantics on top of
// our async HTTP store. inject.ts preloads the snapshot (one bulk GET) before a
// script runs, so the cache is fully populated when the script first reads.

import { deleteUserscriptValue, setUserscriptValue } from "../api";

export interface ValueStore {
  // Synchronous Tampermonkey-style API (no network on the hot path).
  get(key: string, def?: unknown): unknown;
  set(key: string, value: unknown): void;
  delete(key: string): void;
  list(): string[];
  // Promise variants for GM.* — resolve once the network write settles.
  setAsync(key: string, value: unknown): Promise<void>;
  deleteAsync(key: string): Promise<void>;
}

function clone(value: unknown): unknown {
  if (value === undefined) return undefined;
  try {
    return structuredClone(value);
  } catch {
    // Fall back for values structuredClone can't handle (functions, etc.) —
    // GM values are JSON so this is effectively never hit.
    return JSON.parse(JSON.stringify(value));
  }
}

export function createValueStore(scriptId: string, initial: Record<string, unknown>): ValueStore {
  const cache = new Map<string, unknown>(Object.entries(initial));

  // Per-script write chain: set-then-delete reaches the server in call order;
  // reads never wait on it. A failed write is logged but the cache keeps the
  // intended value for the session (LevelDB is durable once a PUT lands).
  let tail: Promise<void> = Promise.resolve();
  function enqueue(op: () => Promise<unknown>): Promise<void> {
    tail = tail.then(() =>
      op().then(
        () => undefined,
        (err) => {
          console.error(`[userscript ${scriptId}] value write failed`, err);
        },
      ),
    );
    return tail;
  }

  return {
    get(key, def) {
      return cache.has(key) ? clone(cache.get(key)) : def;
    },
    set(key, value) {
      cache.set(key, clone(value));
      void enqueue(() => setUserscriptValue(scriptId, key, value));
    },
    delete(key) {
      cache.delete(key);
      void enqueue(() => deleteUserscriptValue(scriptId, key));
    },
    list() {
      return [...cache.keys()];
    },
    setAsync(key, value) {
      cache.set(key, clone(value));
      return enqueue(() => setUserscriptValue(scriptId, key, value));
    },
    deleteAsync(key) {
      cache.delete(key);
      return enqueue(() => deleteUserscriptValue(scriptId, key));
    },
  };
}
