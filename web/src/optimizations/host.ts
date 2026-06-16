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

import { installHooks } from "./registry";
import { applySettings, startStateMachine } from "./state";

let installed = false;

export function installOptimizationHost(): void {
  if (installed) return;
  installed = true;

  if (!window.__bmmHost) {
    window.__bmmHost = {
      version: 1,
      platform: {
        id: "studio-bondage-club",
        name: "Studio Bondage Club",
        version: "1.0",
        capabilities: ["modsdk"],
      },
      onReady: (api) => {
        void onBmmReady(api);
      },
      onEvent: () => {
        // Reserved for lifecycle events (e.g. reloadRequested); the shell reloads
        // itself, so nothing to do for now.
      },
    };
  }
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
  if (window.bcModSdk && typeof window.bcModSdk.registerMod === "function") return window.bcModSdk;
  return null;
}

async function onBmmReady(api: BmmApi): Promise<void> {
  const sdk = resolveSdk(api);
  if (!sdk) {
    console.warn("[optimizations] Mod SDK unavailable; optimizations disabled");
    return;
  }
  const mod = sdk.registerMod({
    name: "SodiumPlus",
    fullName: "Sodium Plus (Studio optimization)",
    version: "1.0",
  });
  if (!mod) {
    console.warn("[optimizations] registerMod failed; optimizations disabled");
    return;
  }
  installHooks(mod);
  startStateMachine();

  // Seed the state machine from the backend. If the panel (same module graph) has
  // already pushed a config via applySettings, that wins — but it also saved that
  // config to the backend first, so this fetch returns the same data either way.
  try {
    applySettings(await getOptimizationSettings());
  } catch (err) {
    console.error("[optimizations] failed to load config", err);
  }
  console.log("[optimizations] installed. Run tps() for stats.");
}
