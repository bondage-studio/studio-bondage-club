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
  /**
   * Cacheable HTTP status codes for this rule. Empty/omitted inherits the global
   * cache.cacheableStatusCodes (which itself defaults to {200, 204, 404}).
   */
  cacheableStatusCodes?: number[];
}

export interface CacheConfig {
  dir: string;
  defaultTTLSeconds: number;
  maxSizeBytes: number;
  stores?: StoreConfig[];
  rules?: CacheRule[];
  cacheableStatusCodes?: number[];
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
  /** Android-app-only settings; only present in the Android build's config. */
  android?: AndroidSettings;
  /** Desktop-app-only settings; only present in the desktop build's config. */
  desktop?: DesktopSettings;
}

export interface AndroidSettings {
  /** Force GeckoView GPU acceleration (WebRender + accelerated 2D canvas). */
  hardwareAcceleration: boolean;
}

export interface DesktopSettings {
  /** Chromium GPU compositing; read at startup, so a change needs a restart. */
  hardwareAcceleration: boolean;
  /** Window size; applied live to the open desktop window. */
  windowWidth: number;
  windowHeight: number;
  /** Persist the OS window size back into config when the user resizes it. */
  rememberWindowSize: boolean;
}

export type ConfigScopeName =
  | "connection"
  | "cache"
  | "gameserver"
  | "gamesettings"
  | "mode"
  | "package"
  | "android"
  | "desktop";

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

// Pushed on the config.subscribe stream whenever a config change is applied
// (from any client, the desktop host, or a direct edit). `changedScopes` maps
// each changed scope to its update tier so the panel can reconcile just those
// sections and surface the right tier message.
export interface ConfigChangedEvent {
  type: "configChanged";
  config: AppConfig;
  changedScopes: Partial<Record<ConfigScopeName, 0 | 1 | 2>>;
  restartRequired: boolean;
  updateTier: 0 | 1 | 2;
  configPath: string;
}

// The first frame on config.subscribe is a full snapshot (a ConfigResponse, no
// `type`); every later frame is a ConfigChangedEvent.
export type ConfigEvent = ConfigResponse | ConfigChangedEvent;

export interface StoreStat {
  name: string;
  stats: CacheStats;
}

/**
 * Cache outcome buckets, derived server-side from the X-Studio-Cache status:
 * - hit: served from cache without contacting upstream (HIT / STALE-HEAD)
 * - revalidated: 304 — cached body reused, only headers refetched
 * - miss: body fetched from upstream and cached
 * - uncached: body fetched but not cacheable (MISS-UNCACHED)
 * - stale: stale cache served after an upstream failure
 * - bypass: request skipped the cache entirely (BYPASS-*)
 */
export const CACHE_OUTCOMES = [
  "hit",
  "revalidated",
  "miss",
  "uncached",
  "stale",
  "bypass",
] as const;

export type CacheOutcome = (typeof CACHE_OUTCOMES)[number];

export type CacheOutcomeMap = Record<CacheOutcome, number>;

/** One per-host (or aggregate) traffic row. `host` is absent on the total row. */
export interface CacheTrafficRow {
  host?: string;
  counts: CacheOutcomeMap;
  bytes: CacheOutcomeMap;
  requests: number;
}

/** Per-host cache hit-rate / bandwidth telemetry since the last reset. */
export interface CacheTraffic {
  /** Epoch-ms when the current measurement window opened (last reset). */
  sinceMs: number;
  /** Epoch-ms when the snapshot was produced. */
  nowMs: number;
  total: CacheTrafficRow;
  /** Rows sorted by request volume, descending. */
  hosts: CacheTrafficRow[];
}

/** One resource (URL) within a host's drill-down, with last-seen debug fields. */
export interface CacheResourceRow {
  url: string;
  counts: CacheOutcomeMap;
  bytes: CacheOutcomeMap;
  requests: number;
  /** Last HTTP status code observed for this URL (0 if unknown). */
  lastStatus: number;
  /** Epoch-ms of the most recent request for this URL. */
  lastMs: number;
}

/** On-demand per-host breakdown (the `cache.trafficDetail` RPC). */
export interface CacheTrafficDetail {
  host: string;
  sinceMs: number;
  nowMs: number;
  /** Host aggregate (same shape as a traffic row). */
  total: CacheTrafficRow;
  /** Resources sorted by request volume, descending. */
  resources: CacheResourceRow[];
}

export interface StatsEvent {
  type: "stats";
  stores: StoreStat[];
  total: CacheStats;
  /** Present on every frame once the backend supports traffic telemetry. */
  traffic?: CacheTraffic;
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

/** Summary of a fetched-but-unapplied update (set by the background checker). */
export interface PendingUpdateSummary {
  version: string;
  fetchedAt: number;
}

/** Full pending update record (includes the fetched source for review). */
export interface PendingUpdate {
  version: string;
  source: string;
  fetchedAt: number;
}

/**
 * A managed userscript. `source` is the full `.user.js` text; metadata (run-at,
 * grants, requires, …) is parsed from it on the client. Stored in a dedicated
 * LevelDB store on the server, decoupled from the app config.
 */
export interface Userscript {
  id: string;
  name: string;
  source: string;
  enabled: boolean;
  autoUpdate: boolean;
  downloadURL?: string;
  updateURL?: string;
  version?: string;
  updatedAt?: number;
  sortOrder?: number;
  /**
   * Built-in default script: its name and source URL are immutable and it cannot
   * be deleted. Source, enable/disable, auto-update, and updates still apply.
   */
  builtin?: boolean;
  /** Present when the background checker has a newer version waiting for review. */
  pendingUpdate?: PendingUpdateSummary;
}

export interface UserscriptSettings {
  updateIntervalHours: number;
}

/**
 * Render-optimization config consumed by the shell loader (src/optimizations).
 * A profile names a set of optimization toggles; an ordered rules list maps a
 * trigger (background / idle / default) to a profile, first match wins. Stored in
 * the userscript LevelDB store under `meta/optimization`.
 */
export const OPTIMIZATION_FEATURE_KEYS = [
  "lazyCanvas",
  "idleFpsThrottle",
  "skipValidation",
  "chatLogTrim",
  "tintCache",
] as const;

export type OptimizationFeatureKey = (typeof OPTIMIZATION_FEATURE_KEYS)[number];

export type OptimizationFeatures = Record<OptimizationFeatureKey, boolean>;

export interface OptimizationProfile {
  id: string;
  name: string;
  features: OptimizationFeatures;
}

export type OptimizationTrigger = "background" | "idle" | "default";

export interface OptimizationRule {
  trigger: OptimizationTrigger;
  /** Only meaningful when `trigger === "idle"`: input-idle threshold in seconds. */
  idleSeconds?: number;
  /** Id of the profile to apply when this rule matches. */
  profile: string;
}

export interface OptimizationSettings {
  /** Master switch: when false the loader installs no hooks at all. */
  enabled: boolean;
  profiles: OptimizationProfile[];
  rules: OptimizationRule[];
}

export interface CheckUpdatesSummary {
  checked: number;
  updates: Array<{
    id: string;
    name: string;
    fromVersion: string;
    toVersion: string;
  }>;
}
