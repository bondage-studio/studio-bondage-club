export type Mode = "reverse_proxy_cache" | "package_cache";

export interface ServerConfig {
  host: string;
  port: number;
  adminBasePath: string;
}

export interface StoreConfig {
  name: string;
  dir?: string;
  maxSizeBytes?: number;
  defaultTTLSeconds?: number;
}

export interface CacheRule {
  host?: string;
  pathPrefix?: string;
  pathPattern?: string;
  store?: string;
  bypass?: boolean;
  ttlSeconds?: number;
  keyMode?: "url" | "path";
  cacheControl?: string;
  forceCache?: boolean;
  /** Version extractor spec: "query:<name>" or "re:<regexp>" over the target URL. */
  version?: string;
  /** "re:<regexp>" matched against the real path to build a canonical cache key. */
  keyPattern?: string;
  /** std::regex_replace template ($1-style refs) producing the canonical key. */
  keyTemplate?: string;
  /**
   * How the version is used. false (default): immutable — folded into the key so
   * each version is its own permanent entry. true: freshness signal for a
   * version-independent key (ETag revalidation on change, e.g. the game body).
   */
  versionRevalidate?: boolean;
}

export interface CacheConfig {
  dir: string;
  defaultTTLSeconds: number;
  maxSizeBytes: number;
  stores?: StoreConfig[];
  rules?: CacheRule[];
}

export interface PackageConfig {
  dir: string;
  manifestUrl: string;
}

export interface GameServerSettings {
  pingIntervalMs: number;
  pingTimeoutMs: number;
  maxPayloadBytes: number;
  messageRatePerSec: number;
  ipConnectionLimit: number;
  ipConnectionRatePerSec: number;
  accountCreatePerDay: number;
  accountCreatePerHour: number;
  loginPaceMs: number;
  loginQueueThreshold: number;
  pbkdf2Iterations: number;
  passwordResetThrottleMs: number;
  relationshipDelayMs: number;
  serverInfoIntervalSec: number;
  delayedFlushIntervalSec: number;
  searchMaxResults: number;
  roomLimitDefault: number;
  roomLimitMin: number;
  roomLimitMax: number;
  descriptionMaxLen: number;
  emailMaxLen: number;
  nameMaxLen: number;
  ownershipNotesMaxLen: number;
}

export interface AppConfig {
  server: ServerConfig;
  mode: Mode;
  upstream: string;
  socks5Proxy: string;
  localGameServer: boolean;
  /** Embedded game DB location; empty means "<cache dir>/gameserver". */
  gameServerStoragePath: string;
  gameServerSettings: GameServerSettings;
  cache: CacheConfig;
  package: PackageConfig;
}

export type ConfigScopeName =
  | "connection"
  | "cache"
  | "gameserver"
  | "gamesettings"
  | "mode"
  | "package";

export interface ScopeUpdateResponse {
  scope: ConfigScopeName;
  slice: Record<string, unknown>;
  /** 0 = applied instantly, 1 = stores rebuilt, 2 = restart required */
  updateTier: 0 | 1 | 2;
  restartRequired: boolean;
  configPath: string;
}

export interface GameServerStatus {
  online: number;
  rooms: number;
}

export interface Capability {
  id: string;
  label: string;
  enabled: boolean;
  description: string;
}

export interface RuntimeStatus {
  mode: Mode;
  upstream?: string;
  cacheDir?: string;
  capabilities: Capability[];
}

export interface CacheStats {
  entries: number;
  bytes: number;
}

export interface ConfigResponse {
  config: AppConfig;
  status: RuntimeStatus;
  cache: CacheStats;
  configPath: string;
  restartRequired: boolean;
  /** 0 = applied instantly, 1 = stores rebuilt, 2 = restart required for address change */
  updateTier?: 0 | 1 | 2;
}

export interface StoreStat {
  name: string;
  stats: CacheStats;
}

export interface StatsEvent {
  type: "stats";
  stores: StoreStat[];
  total: CacheStats;
}

export interface CacheVersion {
  version: string;
  entries: number;
}

export interface ExpireFilter {
  store?: string;
  host?: string;
  pathPrefix?: string;
  version?: string;
}
