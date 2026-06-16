// Ambient declarations for the slice of the BC game globals, the BC Mod SDK, and
// the BMM platform bridge that the optimization loader touches. Keeping them here
// lets sodium.ts / state.ts / host.ts stay strict-typed without scattering casts.

declare global {
  /** The subset of a BC `Character` the hooks read. */
  interface BcCharacter {
    CharacterID?: number;
    Nickname?: string;
    Appearance?: Array<{ Asset?: { Description?: string | null } | null } | null>;
  }

  /** One registered mod, as returned by `ModSDKGlobalAPI.registerMod`. */
  interface ModSDKModAPI {
    hookFunction(
      functionName: string,
      priority: number,
      hook: (args: unknown[], next: (args: unknown[]) => unknown) => unknown,
    ): () => void;
  }

  interface ModSDKModInfo {
    name: string;
    fullName?: string;
    version: string;
  }

  interface ModSDKGlobalAPI {
    registerMod(info: ModSDKModInfo): ModSDKModAPI;
  }

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
    bcModSdk?: ModSDKGlobalAPI;
    __bmmHost?: BmmHost;

    // BC game globals touched by the hooks.
    CurrentScreen?: string;
    CurrentCharacter?: unknown;
    Character?: BcCharacter[];
    ChatRoomViews?: { Character?: { Draw?: unknown } };
    ChatRoomCharacterViewDraw?: unknown;
    CharacterAppearanceBuildCanvas?: (C: BcCharacter) => unknown;
    CharacterRefresh?: (C: BcCharacter, push?: boolean, refreshDialog?: boolean) => void;
    ChatRoomIsViewActive?: (view: string) => boolean;
    tps?: () => void;
  }
}

export {};
