declare global {
  /** `api.sdk` handed to the BMM host bridge via `onReady` (platform-integration). */
  interface BmmSdkApi {
    registerMod(info: ModSDKModInfo): ModSDKModAPI | null;
    get(): ModSDKGlobalAPI | null;
    isHijacked(): boolean;
  }

  interface BmmApi {
    sdk: BmmSdkApi;
  }

  interface BmmHost {
    version?: number;
    platform?: { id: string; name: string; version?: string; capabilities?: string[] };
    settings?: Record<string, unknown>;
    fetch?(url: string, init?: RequestInit): Promise<Response>;
    onReady?(api: BmmApi): void;
    onEvent?(event: { type: string; payload?: unknown }): void;
  }

  /** Staleness state the in-page loader (main.js) may read. The shell injects a
   *  stub (loadedVersion=null) since the cache proxy always serves a current build. */
  interface BmmLoaderState {
    loadedVersion: string | null;
    latestVersion: string | null;
    listeners: Array<(version: string) => void>;
  }

  interface Window {
    __bmmHost?: BmmHost;
    __bmmLoader?: BmmLoaderState;
    /** Set to false at runtime to silence the optimization loader's debug logs. */
    __sodiumDebug?: boolean;
  }
}

export {};
