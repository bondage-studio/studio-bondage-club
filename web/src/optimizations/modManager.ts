const LOADER_URL = "https://bondage-studio.github.io/bc-mod-manager/main.js";
const VERSION_KEY = "bmm:loaderVersion";

let injected = false;

function loaderState(): BmmLoaderState | undefined {
  return window.__bmmLoader;
}

function getVersion(): string | null {
  try {
    return window.localStorage.getItem(VERSION_KEY);
  } catch {
    return null;
  }
}

function setVersion(v: string | null): void {
  try {
    if (v) window.localStorage.setItem(VERSION_KEY, v);
    else window.localStorage.removeItem(VERSION_KEY);
  } catch {
    /* ignore */
  }
}

function notifyLatestVersion(v: string): void {
  const state = loaderState();
  if (!state) return;
  state.latestVersion = v;
  for (const listener of state.listeners) {
    try {
      listener(v);
    } catch {
      /* a bad listener must never break loading */
    }
  }
}

// Dependency-free 53-bit hash (cyrb53) — no crypto.subtle / secure context.
function hash(str: string): string {
  let h1 = 0xdeadbeef;
  let h2 = 0x41c6ce57;
  for (let i = 0; i < str.length; i++) {
    const c = str.charCodeAt(i);
    h1 = Math.imul(h1 ^ c, 2654435761);
    h2 = Math.imul(h2 ^ c, 1597334677);
  }
  h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507);
  h1 ^= Math.imul(h2 ^ (h2 >>> 13), 3266489909);
  h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507);
  h2 ^= Math.imul(h1 ^ (h1 >>> 13), 3266489909);
  return (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(36);
}

async function fetchLoader(url: string, fresh: boolean): Promise<string | null> {
  const host = window.__bmmHost;
  const doFetch = host?.fetch ? host.fetch.bind(host) : window.fetch.bind(window);
  const init: RequestInit = { credentials: "omit" };
  if (fresh) init.cache = "no-cache";
  try {
    const resp = await doFetch(url, init);
    if (!resp || !resp.ok) return null;
    return await resp.text();
  } catch {
    return null;
  }
}

function execute(source: string): void {
  const code = `${source}\n//# sourceURL=bmm-loader/main.js\n`;
  let url = "";
  try {
    const blob = new Blob([code], { type: "text/javascript" });
    url = URL.createObjectURL(blob);
    const el = document.createElement("script");
    el.src = url;
    el.async = false;
    const cleanup = () => {
      if (url) URL.revokeObjectURL(url);
    };
    el.addEventListener("load", cleanup, { once: true });
    el.addEventListener("error", cleanup, { once: true });
    (document.head || document.documentElement).appendChild(el);
  } catch (err) {
    if (url) URL.revokeObjectURL(url);
    console.error("[modManager] failed to execute BC Mod Manager loader", err);
  }
}

async function load(): Promise<void> {
  const state = loaderState();
  const pinned = getVersion();
  if (state) {
    state.loadedVersion = pinned;
    state.latestVersion = pinned;
  }

  const token = pinned || "t" + Math.floor(Date.now() / 3600000);
  let source = await fetchLoader(LOADER_URL + "?v=" + token, false);
  if (source == null) {
    // Fallback (the upstream CORS -> non-CORS retry): drop the possibly-bad pin
    // and fetch a fresh copy.
    setVersion(null);
    if (state) state.loadedVersion = null;
    source = await fetchLoader(LOADER_URL + "?v=r" + Date.now(), true);
  }
  if (source == null) {
    console.error("[modManager] could not fetch BC Mod Manager loader");
    return;
  }

  const v = hash(source);
  if (state) {
    state.loadedVersion = v;
    state.latestVersion = v;
  }
  setVersion(v);
  execute(source);
}

async function validate(): Promise<void> {
  const source = await fetchLoader(LOADER_URL, true);
  if (source == null) return; // offline / blocked: keep the current pin
  const v = hash(source);
  notifyLatestVersion(v);
  if (v !== getVersion()) setVersion(v);
}

export function injectModManager(): void {
  if (injected) return;
  injected = true;

  try {
    Object.defineProperty(window, "bcModSdk", {
      configurable: true,
      enumerable: true,
      get: () => undefined,
      set: () => {
        /* reserved for bmm */
      },
    });
  } catch {
    // already defined — best effort
  }

  window.__bmmLoader = { loadedVersion: null, latestVersion: null, listeners: [] };

  void load();
  // Update check after boot, mirroring the upstream setTimeout(validate, 1500).
  window.setTimeout(() => void validate(), 1500);
}