import {
  Gamepad2,
  HardDrive,
  Package,
  RotateCcw,
  ScrollText,
  Server,
  Settings,
  SlidersHorizontal,
  Smartphone,
  Wifi,
} from "lucide-react";
import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import { clearCache, loadConfig, loadGameServerStatus, resetConfig, saveConfigScope } from "@/api";
import { CacheMigrateDialog } from "@/components/cache/CacheMigrateDialog";
import { RuleEditor } from "@/components/cache/RuleEditor";
import { StoreEditor } from "@/components/cache/StoreEditor";
import { SectionBar } from "@/components/shared/SectionBar";
import { CacheTab } from "@/components/tabs/CacheTab";
import { ConnectionTab } from "@/components/tabs/ConnectionTab";
import { GameServerTab } from "@/components/tabs/GameServerTab";
import { GameSettingsTab } from "@/components/tabs/GameSettingsTab";
import { AndroidTab } from "@/components/tabs/AndroidTab";
import { ModeTab } from "@/components/tabs/ModeTab";
import { PackageTab } from "@/components/tabs/PackageTab";
import { UserscriptsTab } from "@/components/tabs/UserscriptsTab";
import { Button } from "@/components/ui/button";
import { useConfirm } from "@/components/ui/confirm";
import { MasterDetail } from "@/components/ui/master-detail";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Window } from "@/components/ui/window";
import { copyScope, scopeSlice, tierMessages } from "@/config/scope";
import { useSSE } from "@/hooks/useSSE";
import { errorMessage } from "@/lib/utils";
import { IS_ANDROID_BUILD } from "@/lib/platform";
import { getGameServerMode, setGameServerMode, type GameServerMode } from "@/originalPage";
import type {
  AppConfig,
  CacheRule,
  ConfigResponse,
  ConfigScopeName,
  GameServerStatus,
  StoreConfig,
} from "@/types";

type PanelTab = ConfigScopeName | "userscripts";

// Tabs that own their own state and persistence, independent of the form/scope
// machinery (no scopeSlice, no SectionBar save flow).
const SELF_MANAGED_TABS: PanelTab[] = ["userscripts"];

type PageDef = {
  id: PanelTab;
  icon: ReactNode;
  label: string;
  description: string;
  androidHidden?: boolean;
  androidOnly?: boolean;
};

const allPages: PageDef[] = [
  {
    id: "connection",
    icon: <Wifi size={16} />,
    label: "Connection",
    description: "Upstream server, proxy and local listener address.",
  },
  {
    id: "cache",
    icon: <HardDrive size={16} />,
    label: "Cache",
    description: "Storage location, named stores and policy rules.",
  },
  {
    id: "gameserver",
    icon: <Gamepad2 size={16} />,
    label: "Game Server",
    description: "Run the Bondage Club game server locally for offline play.",
  },
  {
    id: "gamesettings",
    icon: <SlidersHorizontal size={16} />,
    label: "Game Settings",
    description: "Tune the embedded game server's limits, intervals and timeouts.",
  },
  {
    id: "mode",
    icon: <Server size={16} />,
    label: "Mode",
    description: "Operating mode and runtime capabilities.",
  },
  {
    id: "package",
    icon: <Package size={16} />,
    label: "Package",
    description: "Package cache directory and manifest source.",
  },
  {
    id: "userscripts",
    icon: <ScrollText size={16} />,
    label: "Userscripts",
    description: "Install and manage Tampermonkey-style userscripts for the game page.",
  },
  {
    id: "android",
    icon: <Smartphone size={16} />,
    label: "Android",
    description: "Settings specific to the Android app.",
    androidOnly: true,
  },
];

// Desktop build drops androidOnly tabs; Android build drops androidHidden tabs.
const pages = allPages.filter((p) => (IS_ANDROID_BUILD ? !p.androidHidden : !p.androidOnly));

function App() {
  const [snapshot, setSnapshot] = useState<ConfigResponse | null>(null);
  const [form, setForm] = useState<AppConfig | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const [open, setOpen] = useState(() => localStorage.getItem("studio-panel-open") !== "false");
  const [tab, setTab] = useState<PanelTab>("connection");

  const confirm = useConfirm();
  const { stats: sseStats, connected } = useSSE();

  const [storeEditorOpen, setStoreEditorOpen] = useState(false);
  const [editingStoreIdx, setEditingStoreIdx] = useState<number | null>(null);

  const [ruleEditorOpen, setRuleEditorOpen] = useState(false);
  const [editingRuleIdx, setEditingRuleIdx] = useState<number | null>(null);

  const [migrateScope, setMigrateScope] = useState<ConfigScopeName | null>(null);

  const [gameStatus, setGameStatus] = useState<GameServerStatus | null>(null);

  // Runtime local/remote game-server switch. Owned by the frontend (persisted to
  // localStorage); flipping it forces the live BC connection to reconnect onto the
  // other endpoint. The backend localGameServer flag is only the first-run default.
  const [serverMode, setServerMode] = useState<GameServerMode>(() => getGameServerMode());

  useEffect(() => {
    void refresh();
  }, []);

  useEffect(() => {
    if (!open || tab !== "gameserver") return;
    let active = true;
    const poll = async () => {
      try {
        const s = await loadGameServerStatus();
        if (active) setGameStatus({ online: s.online, rooms: s.rooms });
      } catch {
        if (active) setGameStatus(null);
      }
    };
    void poll();
    const timer = window.setInterval(poll, 3000);
    return () => {
      active = false;
      window.clearInterval(timer);
    };
  }, [open, tab]);

  function setPanelOpen(next: boolean) {
    setOpen(next);
    localStorage.setItem("studio-panel-open", String(next));
  }

  function switchServerMode(mode: GameServerMode) {
    setServerMode(mode);
    setGameServerMode(mode);
  }

  function sectionDirty(scope: PanelTab): boolean {
    if (SELF_MANAGED_TABS.includes(scope) || !snapshot || !form) return false;
    return (
      JSON.stringify(scopeSlice(form, scope as ConfigScopeName)) !==
      JSON.stringify(scopeSlice(snapshot.config, scope as ConfigScopeName))
    );
  }

  async function refresh() {
    setBusy(true);
    setError("");
    try {
      const next = await loadConfig();
      setSnapshot(next);
      setForm(next.config);
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  // Re-pull just one section's saved values, preserving other sections' edits.
  async function refreshScope(scope: ConfigScopeName) {
    setBusy(true);
    setError("");
    try {
      const fresh = await loadConfig();
      setSnapshot(fresh);
      setForm((prev) => {
        if (!prev) return fresh.config;
        const draft = structuredClone(prev);
        copyScope(draft, fresh.config, scope);
        return draft;
      });
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  async function saveScope(scope: ConfigScopeName, migrate?: boolean) {
    if (!form) return;
    setBusy(true);
    setError("");
    try {
      const resp = await saveConfigScope(
        scope,
        scopeSlice(form, scope),
        migrate ? { migrate: true } : undefined,
      );
      const fresh = await loadConfig();
      setSnapshot(fresh);
      setForm((prev) => {
        if (!prev) return fresh.config;
        const draft = structuredClone(prev);
        copyScope(draft, fresh.config, scope);
        return draft;
      });
      setMessage(tierMessages[resp.updateTier] ?? "Saved");
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  function handleSaveCache() {
    if (!form || !snapshot) return;
    if (form.cache.dir !== snapshot.config.cache.dir) {
      setMigrateScope("cache");
      return;
    }
    void saveScope("cache");
  }

  function handleSaveGameServer() {
    if (!form || !snapshot) return;
    if (form.gameServerStoragePath !== snapshot.config.gameServerStoragePath) {
      setMigrateScope("gameserver");
      return;
    }
    void saveScope("gameserver");
  }

  async function handleClearCache() {
    setBusy(true);
    setError("");
    try {
      await clearCache();
      setMessage("Cache cleared");
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  async function handleResetConfig() {
    const ok = await confirm({
      title: "Reset to defaults?",
      body: "This overwrites your current configuration with the built-in defaults and reloads the app.",
      confirmLabel: "Reset & restart",
      destructive: true,
    });
    if (!ok) return;
    setBusy(true);
    setError("");
    try {
      await resetConfig();
      setMessage("Configuration reset — restarting…");
      window.location.reload();
    } catch (err) {
      setError(errorMessage(err));
      setBusy(false);
    }
  }

  function updateConfig(mutator: (draft: AppConfig) => void) {
    setForm((current) => {
      if (!current) return current;
      const draft = structuredClone(current);
      mutator(draft);
      return draft;
    });
  }

  function openAddStore() {
    setEditingStoreIdx(null);
    setStoreEditorOpen(true);
  }
  function openEditStore(idx: number) {
    setEditingStoreIdx(idx);
    setStoreEditorOpen(true);
  }
  function handleSaveStore(store: StoreConfig) {
    updateConfig((draft) => {
      draft.cache.stores ??= [];
      if (editingStoreIdx !== null) {
        draft.cache.stores[editingStoreIdx] = store;
      } else {
        draft.cache.stores.push(store);
      }
    });
    setStoreEditorOpen(false);
  }
  function handleDeleteStore(idx: number) {
    updateConfig((draft) => {
      draft.cache.stores?.splice(idx, 1);
    });
  }

  function openAddRule() {
    setEditingRuleIdx(null);
    setRuleEditorOpen(true);
  }
  function openEditRule(idx: number) {
    setEditingRuleIdx(idx);
    setRuleEditorOpen(true);
  }
  function handleSaveRule(rule: CacheRule) {
    updateConfig((draft) => {
      draft.cache.rules ??= [];
      if (editingRuleIdx !== null) {
        draft.cache.rules[editingRuleIdx] = rule;
      } else {
        draft.cache.rules.push(rule);
      }
    });
    setRuleEditorOpen(false);
  }
  function handleDeleteRule(idx: number) {
    updateConfig((draft) => {
      draft.cache.rules?.splice(idx, 1);
    });
  }
  function handleMoveRule(idx: number, dir: -1 | 1) {
    updateConfig((draft) => {
      const rules = draft.cache.rules;
      if (!rules) return;
      const target = idx + dir;
      if (target < 0 || target >= rules.length) return;
      [rules[idx], rules[target]] = [rules[target], rules[idx]];
    });
  }

  if (!open) {
    return (
      <button
        className="fixed bottom-4 right-4 z-40 grid h-12 w-12 place-items-center rounded-full border border-border bg-background shadow-lg hover:bg-accent transition-colors"
        onClick={() => setPanelOpen(true)}
        title="Open Studio panel"
      >
        <Settings size={20} className="text-foreground" />
      </button>
    );
  }

  const stores = form?.cache.stores ?? [];
  const rules = form?.cache.rules ?? [];
  const editingStore = editingStoreIdx !== null ? stores[editingStoreIdx] : undefined;
  const editingRule = editingRuleIdx !== null ? rules[editingRuleIdx] : undefined;
  const activePage = pages.find((p) => p.id === tab)!;

  const footer = (
    <footer className="flex h-10 shrink-0 items-center justify-between gap-3 border-t bg-muted px-3">
      <div className="min-w-0 flex-1 truncate text-xs">
        {error ? (
          <span className="text-destructive">{error}</span>
        ) : snapshot?.restartRequired ? (
          <span className="text-amber-700">Listener address changed — restart to apply.</span>
        ) : message ? (
          <span className="text-emerald-700">{message}</span>
        ) : (
          <span className="text-muted-foreground">Each section saves independently.</span>
        )}
      </div>
      <Button
        variant="outline"
        size="sm"
        className="shrink-0"
        disabled={busy || !snapshot}
        onClick={() => void handleResetConfig()}
        title="Reset all settings to defaults and restart"
      >
        <RotateCcw size={13} />
        Reset to defaults
      </Button>
    </footer>
  );

  return (
    <>
      <Window
        documentTitle="Studio Bondage Club — Configuration"
        poppable
        onClose={() => setPanelOpen(false)}
      >
        <Window.Title icon={<Settings size={14} className="text-muted-foreground" />}>
          Studio Bondage Club — Configuration
        </Window.Title>
        <Window.Footer>{footer}</Window.Footer>
        <Window.Body>
          <MasterDetail
            items={pages.map(({ id, icon, label, description }) => ({
              key: id,
              icon,
              label,
              description,
              badge: sectionDirty(id),
            }))}
            activeKey={tab}
            onSelect={(key) => setTab(key as PanelTab)}
            backLabel={activePage.label}
            detail={
              <>
                <div className="flex shrink-0 items-center justify-between gap-3 border-b bg-background px-4 py-2">
                  <div className="min-w-0">
                    <h2 className="text-sm font-semibold leading-tight">{activePage.label}</h2>
                    <p className="truncate text-xs text-muted-foreground">
                      {activePage.description}
                    </p>
                  </div>
                  {!SELF_MANAGED_TABS.includes(tab) && (
                    <SectionBar
                      dirty={sectionDirty(tab)}
                      busy={busy}
                      onRefresh={() => void refreshScope(tab as ConfigScopeName)}
                      onSave={
                        tab === "cache"
                          ? handleSaveCache
                          : tab === "gameserver"
                            ? handleSaveGameServer
                            : () => void saveScope(tab as ConfigScopeName)
                      }
                    />
                  )}
                </div>

                <ScrollArea className="min-h-0 flex-1">
                  <div className="@container p-3">
                    {tab === "userscripts" ? (
                      <UserscriptsTab />
                    ) : !form || !snapshot ? (
                      <p className="py-8 text-center text-sm text-muted-foreground">Loading…</p>
                    ) : (
                      <>
                        {tab === "connection" && (
                          <ConnectionTab form={form} onChange={updateConfig} />
                        )}
                        {tab === "cache" && (
                          <CacheTab
                            form={form}
                            connected={connected}
                            sseStats={sseStats}
                            busy={busy}
                            onChange={updateConfig}
                            onClearCache={() => void handleClearCache()}
                            onAddStore={openAddStore}
                            onEditStore={openEditStore}
                            onDeleteStore={handleDeleteStore}
                            onAddRule={openAddRule}
                            onEditRule={openEditRule}
                            onDeleteRule={handleDeleteRule}
                            onMoveRule={handleMoveRule}
                          />
                        )}
                        {tab === "gameserver" && (
                          <GameServerTab
                            form={form}
                            serverMode={serverMode}
                            gameStatus={gameStatus}
                            onSwitchMode={switchServerMode}
                            onChange={updateConfig}
                          />
                        )}
                        {tab === "gamesettings" && (
                          <GameSettingsTab form={form} onChange={updateConfig} />
                        )}
                        {tab === "mode" && (
                          <ModeTab form={form} snapshot={snapshot} onChange={updateConfig} />
                        )}
                        {tab === "package" && (
                          <PackageTab form={form} snapshot={snapshot} onChange={updateConfig} />
                        )}
                        {IS_ANDROID_BUILD && tab === "android" && (
                          <AndroidTab form={form} onChange={updateConfig} />
                        )}
                      </>
                    )}
                  </div>
                </ScrollArea>
              </>
            }
          />
        </Window.Body>
      </Window>

      {/* Editors mount after the main window so they stack above it by DOM order. */}
      {form && storeEditorOpen && (
        <StoreEditor
          key={editingStoreIdx ?? "new-store"}
          initial={editingStore}
          onSave={handleSaveStore}
          onClose={() => setStoreEditorOpen(false)}
        />
      )}
      {form && ruleEditorOpen && (
        <RuleEditor
          key={editingRuleIdx ?? "new-rule"}
          initial={editingRule}
          stores={stores}
          onSave={handleSaveRule}
          onClose={() => setRuleEditorOpen(false)}
        />
      )}
      {form && snapshot && migrateScope === "cache" && (
        <CacheMigrateDialog
          oldDir={snapshot.config.cache.dir}
          newDir={form.cache.dir}
          onChoose={(migrate) => {
            setMigrateScope(null);
            void saveScope("cache", migrate);
          }}
          onClose={() => setMigrateScope(null)}
        />
      )}
      {form && snapshot && migrateScope === "gameserver" && (
        <CacheMigrateDialog
          title="Move game accounts?"
          intro="The game server storage path is changing:"
          oldDir={
            snapshot.config.gameServerStoragePath ||
            `${snapshot.config.cache.dir || "."}/gameserver`
          }
          newDir={form.gameServerStoragePath || `${form.cache.dir || "."}/gameserver`}
          body={
            <>
              Migrate the existing <strong>local game accounts</strong> to the new location, or
              start fresh there? Live game sockets reconnect either way.
            </>
          }
          onChoose={(migrate) => {
            setMigrateScope(null);
            void saveScope("gameserver", migrate);
          }}
          onClose={() => setMigrateScope(null)}
        />
      )}
    </>
  );
}

export default App;
