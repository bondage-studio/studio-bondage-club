import type { AppConfig, ConfigScopeName } from "@/types";

export const tierMessages: Record<number, string> = {
  0: "Applied instantly",
  1: "Cache rebuilt",
  2: "Restart required to take effect",
};

// scopeSlice extracts the JSON body PUT to /api/config/{scope}. It mirrors the
// backend ConfigScope::get so dirty-detection and saving stay in sync. (The
// remote `gameServer` URL is intentionally omitted from the connection slice —
// the UI doesn't edit it, and the backend leaves absent keys untouched.)
export function scopeSlice(cfg: AppConfig, scope: ConfigScopeName): unknown {
  switch (scope) {
    case "connection":
      return {
        host: cfg.server.host,
        port: cfg.server.port,
        adminBasePath: cfg.server.adminBasePath,
        upstream: cfg.upstream,
        socks5Proxy: cfg.socks5Proxy,
      };
    case "cache":
      return cfg.cache;
    case "gameserver":
      return {
        localGameServer: cfg.localGameServer,
        gameServerStoragePath: cfg.gameServerStoragePath,
      };
    case "gamesettings":
      return cfg.gameServerSettings;
    case "mode":
      return { mode: cfg.mode };
    case "package":
      return cfg.package;
    case "android":
      return cfg.android ?? { hardwareAcceleration: true };
  }
}

// copyScope overwrites just `scope`'s fields in dst from src, leaving every
// other section's (possibly unsaved) edits intact.
export function copyScope(dst: AppConfig, src: AppConfig, scope: ConfigScopeName): void {
  switch (scope) {
    case "connection":
      dst.server.host = src.server.host;
      dst.server.port = src.server.port;
      dst.server.adminBasePath = src.server.adminBasePath;
      dst.upstream = src.upstream;
      dst.socks5Proxy = src.socks5Proxy;
      break;
    case "cache":
      dst.cache = structuredClone(src.cache);
      break;
    case "gameserver":
      dst.localGameServer = src.localGameServer;
      dst.gameServerStoragePath = src.gameServerStoragePath;
      break;
    case "gamesettings":
      dst.gameServerSettings = structuredClone(src.gameServerSettings);
      break;
    case "mode":
      dst.mode = src.mode;
      break;
    case "package":
      dst.package = structuredClone(src.package);
      break;
    case "android":
      dst.android = src.android ? structuredClone(src.android) : undefined;
      break;
  }
}
