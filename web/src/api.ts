import type {
  ConfigResponse,
  ConfigScopeName,
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
 * Save a single config scope (per-section). The server merges the slice into the
 * live config, validates the whole, and fires reload work only for this scope.
 * `migrate` is honoured only by the cache scope when its directory changed.
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

export function loadGameServerStatus(): Promise<GameServerStatus & { enabled: boolean }> {
  return request<GameServerStatus & { enabled: boolean }>("/api/gameserver/status");
}

