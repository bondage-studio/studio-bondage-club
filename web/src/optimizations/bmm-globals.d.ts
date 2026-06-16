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
    onReady?(api: BmmApi): void;
    onEvent?(event: { type: string; payload?: unknown }): void;
  }

  interface Window {
    __bmmHost?: BmmHost;
    /** Set to false at runtime to silence the optimization loader's debug logs. */
    __sodiumDebug?: boolean;
  }
}

export {};