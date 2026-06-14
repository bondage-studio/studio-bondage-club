import type {
  CacheVersion,
  ConfigResponse,
  ConfigScopeName,
  ExpireFilter,
  GameServerStatus,
  ScopeUpdateResponse
} from "./types";

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    headers: {
      "Content-Type": "application/json",
      ...(init?.headers ?? {})
    }
  });
  if (!response.ok) {
    let message = `${response.status} ${response.statusText}`;
    try {
      const body = (await response.json()) as { error?: string };
      if (body.error) {
        message = body.error;
      }
    } catch {
      // Keep the HTTP status as the error message.
    }
    throw new Error(message);
  }
  return (await response.json()) as T;
}

export function loadConfig(): Promise<ConfigResponse> {
  return request<ConfigResponse>("/api/config");
}

/**
 * Reset the entire configuration back to the built-in defaults. The server
 * persists and hot-applies them; the caller typically reloads the app afterwards
 * so the game restarts against the fresh config.
 */
export function resetConfig(): Promise<ConfigResponse> {
  return request<ConfigResponse>("/api/config/reset", { method: "POST" });
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
  opts?: { migrate?: boolean }
): Promise<ScopeUpdateResponse> {
  const query = opts?.migrate ? "?migrate=true" : "";
  return request<ScopeUpdateResponse>(`/api/config/${scope}${query}`, {
    method: "PUT",
    body: JSON.stringify(slice)
  });
}

export function clearCache(): Promise<{ ok: boolean }> {
  return request<{ ok: boolean }>("/api/cache/clear", { method: "POST" });
}

/**
 * Soft-expire matching cache entries (keeps bodies so the next request
 * revalidates via ETag). Empty filter fields match anything. Returns the count.
 */
export function expireCache(filter: ExpireFilter): Promise<{ ok: boolean; expired: number }> {
  return request<{ ok: boolean; expired: number }>("/api/cache/expire", {
    method: "POST",
    body: JSON.stringify(filter)
  });
}

/** List distinct source versions held in a store (empty = aggregate all stores). */
export function listVersions(store?: string): Promise<CacheVersion[]> {
  const query = store ? `?store=${encodeURIComponent(store)}` : "";
  return request<CacheVersion[]>(`/api/cache/versions${query}`);
}

export function loadGameServerStatus(): Promise<GameServerStatus & { enabled: boolean }> {
  return request<GameServerStatus & { enabled: boolean }>("/api/gameserver/status");
}

