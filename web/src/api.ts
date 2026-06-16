import { rpcClient } from "@/rpc/client";
import type {
  CacheVersion,
  CheckUpdatesSummary,
  ConfigResponse,
  ConfigScopeName,
  ExpireFilter,
  GameServerStatus,
  PendingUpdate,
  ScopeUpdateResponse,
  OptimizationSettings,
  Userscript,
  UserscriptSettings,
} from "@/types";

// All backend API is served over the capability-gated RPC channel (see
// src/rpc/*). Each function maps to one RPC method; signatures and return types
// are unchanged from the previous REST client, so callers are unaffected.

export function loadConfig(): Promise<ConfigResponse> {
  return rpcClient.call<ConfigResponse>("config.get");
}

/**
 * Reset the entire configuration back to the built-in defaults. The server
 * persists and hot-applies them; the caller typically reloads the app afterwards
 * so the game restarts against the fresh config.
 */
export function resetConfig(): Promise<ConfigResponse> {
  return rpcClient.call<ConfigResponse>("config.reset");
}

/**
 * Save a single config scope (per-section). The server merges the slice into the
 * live config, validates the whole, and fires reload work only for this scope.
 * `migrate` is honoured by the cache scope (cache dir change) and the gameserver
 * scope (game storage path change) to move existing data to the new location.
 */
export function saveConfigScope(
  scope: ConfigScopeName,
  slice: unknown,
  opts?: { migrate?: boolean },
): Promise<ScopeUpdateResponse> {
  return rpcClient.call<ScopeUpdateResponse>("config.updateScope", {
    scope,
    slice,
    migrate: opts?.migrate ?? false,
  });
}

export function clearCache(): Promise<{ ok: boolean }> {
  return rpcClient.call<{ ok: boolean }>("cache.clear");
}

/**
 * Soft-expire matching cache entries (keeps bodies so the next request
 * revalidates via ETag). Empty filter fields match anything. Returns the count.
 */
export function expireCache(filter: ExpireFilter): Promise<{ ok: boolean; expired: number }> {
  return rpcClient.call<{ ok: boolean; expired: number }>("cache.expire", filter);
}

/** List distinct source versions held in a store (empty = aggregate all stores). */
export function listVersions(store?: string): Promise<CacheVersion[]> {
  return rpcClient.call<CacheVersion[]>("cache.versions", store ? { store } : {});
}

export function loadGameServerStatus(): Promise<GameServerStatus & { enabled: boolean }> {
  return rpcClient.call<GameServerStatus & { enabled: boolean }>("gameserver.status");
}

// A dedicated API (not a config scope): the Userscripts tab owns its own state
// and persistence, decoupled from the form/scope machinery.

export function listUserscripts(): Promise<Userscript[]> {
  return rpcClient.call<Userscript[]>("userscripts.list");
}

export function saveUserscript(script: Userscript): Promise<Userscript> {
  return rpcClient.call<Userscript>("userscripts.save", script);
}

export function deleteUserscript(id: string): Promise<{ ok: boolean }> {
  return rpcClient.call<{ ok: boolean }>("userscripts.delete", { script: id });
}

export function reorderUserscripts(ids: string[]): Promise<{ ok: boolean }> {
  return rpcClient.call<{ ok: boolean }>("userscripts.reorder", { ids });
}

export function getUserscriptValues(id: string): Promise<Record<string, unknown>> {
  return rpcClient.call<Record<string, unknown>>("userscripts.values.get", { script: id });
}

export function setUserscriptValue(id: string, key: string, value: unknown): Promise<unknown> {
  return rpcClient.call("userscripts.values.set", { script: id, key, value });
}

export function deleteUserscriptValue(id: string, key: string): Promise<unknown> {
  return rpcClient.call("userscripts.values.delete", { script: id, key });
}

export function getUserscriptSettings(): Promise<UserscriptSettings> {
  return rpcClient.call<UserscriptSettings>("userscripts.settings.get");
}

export function saveUserscriptSettings(settings: UserscriptSettings): Promise<UserscriptSettings> {
  return rpcClient.call<UserscriptSettings>("userscripts.settings.set", settings);
}

export function checkUserscriptUpdates(): Promise<CheckUpdatesSummary> {
  return rpcClient.call<CheckUpdatesSummary>("userscripts.checkUpdates");
}

export function getOptimizationSettings(): Promise<OptimizationSettings> {
  return rpcClient.call<OptimizationSettings>("userscripts.optimization.get");
}

export function saveOptimizationSettings(
  settings: OptimizationSettings,
): Promise<OptimizationSettings> {
  return rpcClient.call<OptimizationSettings>("userscripts.optimization.set", settings);
}

export function getPendingUpdate(id: string): Promise<PendingUpdate> {
  return rpcClient.call<PendingUpdate>("userscripts.pending", { script: id });
}

export function applyUserscriptUpdate(id: string): Promise<Userscript> {
  return rpcClient.call<Userscript>("userscripts.applyUpdate", { script: id });
}

export function dismissUserscriptUpdate(id: string): Promise<{ ok: boolean }> {
  return rpcClient.call<{ ok: boolean }>("userscripts.dismissUpdate", { script: id });
}
