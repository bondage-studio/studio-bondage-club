import {
  Gamepad2,
  HardDrive,
  Package,
  Server,
  Settings,
  SlidersHorizontal,
  Wifi
} from "lucide-react";
import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import { clearCache, loadConfig, loadGameServerStatus, saveConfigScope } from "./api";
import { CacheMigrateDialog } from "./components/cache/CacheMigrateDialog";
import { RuleEditor } from "./components/cache/RuleEditor";
import { StoreEditor } from "./components/cache/StoreEditor";
import { SectionBar } from "./components/shared/SectionBar";
import { CacheTab } from "./components/tabs/CacheTab";
import { ConnectionTab } from "./components/tabs/ConnectionTab";
import { GameServerTab } from "./components/tabs/GameServerTab";
import { GameSettingsTab } from "./components/tabs/GameSettingsTab";
import { ModeTab } from "./components/tabs/ModeTab";
import { PackageTab } from "./components/tabs/PackageTab";
import { ScrollArea } from "./components/ui/scroll-area";
import { Window } from "./components/ui/window";
import { copyScope, scopeSlice, tierMessages } from "./config/scope";
import { useSSE } from "./hooks/useSSE";
import { cn, errorMessage } from "./lib/utils";
import { getGameServerMode, setGameServerMode, type GameServerMode } from "./originalPage";
import type {
  AppConfig,
  CacheRule,
  ConfigResponse,
  ConfigScopeName,
  GameServerStatus,
  StoreConfig
} from "./types";

// Each tab maps 1:1 to a backend config scope, so its Save/Refresh act only on
// that slice (PUT /api/config/{scope}) without touching the others.
type PanelTab = ConfigScopeName;

const pages: { id: PanelTab; icon: ReactNode; label: string; description: string }[] = [
  {
    id: "connection",
    icon: <Wifi size={16} />,
    label: "Connection",
    description: "Upstream server, proxy and local listener address."
  },
  {
    id: "cache",
    icon: <HardDrive size={16} />,
    label: "Cache",
    description: "Storage location, named stores and policy rules."
  },
  {
    id: "gameserver",
    icon: <Gamepad2 size={16} />,
    label: "Game Server",
    description: "Run the Bondage Club game server locally for offline play."
  },
  {
    id: "gamesettings",
    icon: <SlidersHorizontal size={16} />,
    label: "Game Settings",
    description: "Tune the embedded game server's limits, intervals and timeouts."
  },
  {
    id: "mode",
    icon: <Server size={16} />,
    label: "Mode",
    description: "Operating mode and runtime capabilities."
  },
  {
    id: "package",
    icon: <Package size={16} />,
    label: "Package",
    description: "Package cache directory and manifest source."
  }
];

function App() {
  const [snapshot, setSnapshot] = useState<ConfigResponse | null>(null);
  const [form, setForm] = useState<AppConfig | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const [open, setOpen] = useState(() => localStorage.getItem("studio-panel-open") !== "false");
  const [tab, setTab] = useState<PanelTab>("connection");

  const { stats: sseStats, connected } = useSSE();

  // Store editor state
  const [storeEditorOpen, setStoreEditorOpen] = useState(false);
  const [editingStoreIdx, setEditingStoreIdx] = useState<number | null>(null);

  // Rule editor state
  const [ruleEditorOpen, setRuleEditorOpen] = useState(false);
  const [editingRuleIdx, setEditingRuleIdx] = useState<number | null>(null);

  // Cache-dir migration prompt (shown when the cache directory changed on save).
  const [migratePrompt, setMigratePrompt] = useState(false);

  // Game server live status (polled while the Game Server tab is open).
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

  function sectionDirty(scope: ConfigScopeName): boolean {
    if (!snapshot || !form) return false;
    return (
      JSON.stringify(scopeSlice(form, scope)) !==
      JSON.stringify(scopeSlice(snapshot.config, scope))
    );
  }

  // Initial full load (and a hard reset of every section).
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
        migrate ? { migrate: true } : undefined
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

  // Cache save intercepts a directory change to ask about migrating data.
  function handleSaveCache() {
    if (!form || !snapshot) return;
    if (form.cache.dir !== snapshot.config.cache.dir) {
      setMigratePrompt(true);
      return;
    }
    void saveScope("cache");
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

  function updateConfig(mutator: (draft: AppConfig) => void) {
    setForm((current) => {
      if (!current) return current;
      const draft = structuredClone(current);
      mutator(draft);
      return draft;
    });
  }

  // Store CRUD
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

  // Rule CRUD
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
        <Window.Body className="flex-row">
          {/* Category sidebar */}
          <nav className="flex w-40 shrink-0 flex-col gap-px border-r bg-muted p-1.5">
            {pages.map(({ id, icon, label }) => {
              const active = tab === id;
              const showDot = sectionDirty(id);
              return (
                <button
                  key={id}
                  onClick={() => setTab(id)}
                  className={cn(
                    "relative flex items-center gap-2 rounded-md px-2.5 py-1.5 text-left text-sm transition-colors",
                    active
                      ? "bg-primary/10 font-medium text-primary"
                      : "text-muted-foreground hover:bg-accent hover:text-foreground"
                  )}
                >
                  {active && (
                    <span className="absolute inset-y-1 left-0 w-0.5 rounded-r-full bg-primary" />
                  )}
                  <span
                    className={cn(
                      "shrink-0",
                      active ? "text-primary" : "text-muted-foreground"
                    )}
                  >
                    {icon}
                  </span>
                  {label}
                  {showDot && (
                    <span
                      className="ml-auto h-1.5 w-1.5 shrink-0 rounded-full bg-amber-500"
                      title="Unsaved changes"
                    />
                  )}
                </button>
              );
            })}
          </nav>

          {/* Content pane */}
          <div className="flex min-w-0 flex-1 flex-col">
            <div className="flex shrink-0 items-center justify-between gap-3 border-b bg-background px-4 py-2">
              <div className="min-w-0">
                <h2 className="text-sm font-semibold leading-tight">{activePage.label}</h2>
                <p className="truncate text-xs text-muted-foreground">
                  {activePage.description}
                </p>
              </div>
              <SectionBar
                dirty={sectionDirty(tab)}
                busy={busy}
                onRefresh={() => void refreshScope(tab)}
                onSave={tab === "cache" ? handleSaveCache : () => void saveScope(tab)}
              />
            </div>

            <ScrollArea className="min-h-0 flex-1">
              <div className="p-3">
                {!form || !snapshot ? (
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
                        serverMode={serverMode}
                        gameStatus={gameStatus}
                        onSwitchMode={switchServerMode}
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
                  </>
                )}
              </div>
            </ScrollArea>
          </div>
        </Window.Body>
      </Window>

      {/* Editor windows — mounted only while open so each open starts from a
          fresh form derived from the item being edited. They render after the
          main window, so they stack above it by DOM order. */}
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
      {form && snapshot && migratePrompt && (
        <CacheMigrateDialog
          oldDir={snapshot.config.cache.dir}
          newDir={form.cache.dir}
          onChoose={(migrate) => {
            setMigratePrompt(false);
            void saveScope("cache", migrate);
          }}
          onClose={() => setMigratePrompt(false)}
        />
      )}
    </>
  );
}

export default App;