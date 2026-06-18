// The studio platform bridge for BC Mod Manager. Advertises studio-bondage-club to
// BMM via window.__bmmHost (see BMM's platform-integration docs); BMM hands back the
// authoritative Mod SDK through onReady(api), which we use to register the Sodium
// optimizations. Must run at document-start, before the BMM loader injects its
// bundle — installOptimizationHost() is called at the top of restoreParsedHomepage.
//
// Hooks are installed once (when the SDK is available) and gated entirely by the
// live flags, so every toggle — including the master `enabled` switch — takes effect
// without re-hooking: a disabled config just resolves to the all-off feature set.

import { getOptimizationSettings } from "@/api";
import { toProxyURL } from "@/userscripts/metadata";

import { dbg } from "./debug";
import { installHooks } from "./registry";
import { applySettings, startStateMachine } from "./state";

let installed = false;

// A RequestCache mode that asks to skip / revalidate the cache rather than reuse
// a stored response.
function isBypassCacheMode(mode: RequestCache | undefined): boolean {
  return mode === "no-cache" || mode === "reload" || mode === "no-store";
}

function studioHostFetch(url: string, init?: RequestInit): Promise<Response> {
  if (!isBypassCacheMode(init?.cache)) {
    return window.fetch(url, init);
  }
  const proxied = toProxyURL(url);
  if (proxied === url) {
    return window.fetch(url, init);
  }
  const headers = new Headers(init?.headers);
  if (!headers.has("Cache-Control")) {
    headers.set("Cache-Control", "no-cache");
  }
  return window.fetch(proxied, { ...init, headers });
}

export function installOptimizationHost(): void {
  dbg("installOptimizationHost called");
  if (installed) return;
  installed = true;

  if (window.__bmmHost) {
    dbg("window.__bmmHost already present; not registering our bridge (onReady won't fire for us)");
    return;
  }
  dbg("installing BMM host bridge");
  window.__bmmHost = {
    version: 1,
    platform: {
      id: "studio-bondage-club",
      name: "Studio Bondage Club",
      version: "1.0",
      capabilities: ["modsdk"],
    },
    fetch: (url, init) => studioHostFetch(url, init),
    onReady: (api) => {
      dbg("BMM onReady fired");
      void onBmmReady(api);
    },
    onEvent: () => {
      // Reserved for lifecycle events (e.g. reloadRequested); the shell reloads
      // itself, so nothing to do for now.
    },
  };
}

interface SdkLike {
  registerMod(info: ModSDKModInfo): ModSDKModAPI | null;
}

// Resolve the authoritative SDK BMM handed us. Per BMM's docs we go through
// api.sdk rather than reading window.bcModSdk (which may be a replaced instance).
function resolveSdk(api: BmmApi): SdkLike | null {
  const sdk = api.sdk;
  if (sdk) {
    if (typeof sdk.isHijacked === "function" && sdk.isHijacked()) {
      console.warn("[optimizations] BC's bundled SDK won the race; using the hijacked instance");
    }
    if (typeof sdk.registerMod === "function") return sdk;
    const raw = typeof sdk.get === "function" ? sdk.get() : null;
    if (raw && typeof raw.registerMod === "function") return raw;
  }
  if (typeof bcModSdk !== "undefined" && typeof bcModSdk.registerMod === "function")
    return bcModSdk;
  return null;
}

async function onBmmReady(api: BmmApi): Promise<void> {
  const sdk = resolveSdk(api);
  if (!sdk) {
    console.warn("[optimizations] Mod SDK unavailable; optimizations disabled");
    return;
  }
  dbg("SDK resolved; registering mod");
  const mod = sdk.registerMod({
    name: "SodiumPlus",
    fullName: "Sodium Plus (Studio optimization)",
    version: "1.0",
  });
  if (!mod) {
    console.warn("[optimizations] registerMod failed; optimizations disabled");
    return;
  }
  dbg("mod registered");
  // Hooks install once BC's functions exist;
  void installHooks(mod);
  startStateMachine();

  // Seed the state machine from the backend. If the panel (same module graph) has
  // already pushed a config via applySettings, that wins — but it also saved that
  // config to the backend first, so this fetch returns the same data either way.
  try {
    applySettings(await getOptimizationSettings());
  } catch (err) {
    console.error("[optimizations] failed to load config", err);
  }
  console.log("[optimizations] installed. See the Optimizations tab for live stats.");
}
