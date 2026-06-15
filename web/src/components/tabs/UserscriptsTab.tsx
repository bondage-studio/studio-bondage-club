import { ChevronDown, ChevronUp, Download, Pencil, Plus, RefreshCw, Trash2 } from "lucide-react";
import { useEffect, useState } from "react";
import {
  applyUserscriptUpdate,
  checkUserscriptUpdates,
  deleteUserscript,
  dismissUserscriptUpdate,
  getUserscriptSettings,
  listUserscripts,
  reorderUserscripts,
  saveUserscript,
  saveUserscriptSettings,
} from "../../api";
import { errorMessage } from "../../lib/utils";
import { listMenuCommands, onMenuChange, type MenuCommand } from "../../userscripts/menu";
import { parseMetadata } from "../../userscripts/metadata";
import type { Userscript } from "../../types";
import { Badge } from "../ui/badge";
import { Button } from "../ui/button";
import { useConfirm } from "../ui/confirm";
import { Input } from "../ui/input";
import { Switch } from "../ui/switch";
import { Panel } from "../shared/Panel";
import { PendingUpdateDialog } from "../userscripts/PendingUpdateDialog";
import { UserscriptEditor } from "../userscripts/UserscriptEditor";

export function UserscriptsTab() {
  const confirm = useConfirm();
  const [scripts, setScripts] = useState<Userscript[]>([]);
  const [intervalHours, setIntervalHours] = useState(6);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");

  const [editorOpen, setEditorOpen] = useState(false);
  const [editing, setEditing] = useState<Userscript | undefined>(undefined);
  const [reviewing, setReviewing] = useState<Userscript | undefined>(undefined);

  const [menuCommands, setMenuCommands] = useState<MenuCommand[]>(() => listMenuCommands());

  useEffect(() => {
    void reload();
  }, []);

  // Keep the Menu Commands section live as scripts register/unregister.
  useEffect(() => {
    setMenuCommands(listMenuCommands());
    return onMenuChange(() => setMenuCommands(listMenuCommands()));
  }, []);

  async function reload() {
    setLoading(true);
    setError("");
    try {
      const [list, settings] = await Promise.all([listUserscripts(), getUserscriptSettings()]);
      setScripts(list);
      setIntervalHours(settings.updateIntervalHours);
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setLoading(false);
    }
  }

  async function withBusy(action: () => Promise<void>, okMessage?: string) {
    setBusy(true);
    setError("");
    setMessage("");
    try {
      await action();
      if (okMessage) setMessage(okMessage);
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  function saveScript(script: Userscript) {
    return withBusy(async () => {
      await saveUserscript(script);
      await reload();
    });
  }

  async function toggle(script: Userscript, patch: Partial<Userscript>) {
    await withBusy(async () => {
      await saveUserscript({ ...script, ...patch });
      await reload();
    });
  }

  async function remove(script: Userscript) {
    const ok = await confirm({
      title: "Delete userscript?",
      body: `"${script.name}" and its stored values will be permanently removed.`,
      confirmLabel: "Delete",
      destructive: true,
    });
    if (!ok) return;
    await withBusy(async () => {
      await deleteUserscript(script.id);
      await reload();
    });
  }

  async function move(index: number, dir: -1 | 1) {
    const next = index + dir;
    if (next < 0 || next >= scripts.length) return;
    const reordered = scripts.slice();
    [reordered[index], reordered[next]] = [reordered[next], reordered[index]];
    setScripts(reordered);
    await withBusy(async () => {
      await reorderUserscripts(reordered.map((s) => s.id));
      await reload();
    });
  }

  function checkUpdates() {
    return withBusy(async () => {
      const summary = await checkUserscriptUpdates();
      await reload();
      setMessage(
        summary.updates.length > 0
          ? `${summary.updates.length} update(s) available.`
          : `Checked ${summary.checked} script(s); all up to date.`,
      );
    });
  }

  function saveInterval() {
    return withBusy(async () => {
      await saveUserscriptSettings({ updateIntervalHours: intervalHours });
    }, "Interval saved.");
  }

  function applyUpdate(script: Userscript) {
    return withBusy(async () => {
      await applyUserscriptUpdate(script.id);
      await reload();
    });
  }

  function dismissUpdate(script: Userscript) {
    return withBusy(async () => {
      await dismissUserscriptUpdate(script.id);
      await reload();
    });
  }

  return (
    <div className="grid max-w-3xl gap-3">
      <Panel
        title="Scripts"
        action={
          <div className="flex items-center gap-1.5">
            <Button variant="outline" size="sm" onClick={() => void checkUpdates()} disabled={busy}>
              <RefreshCw size={13} />
              Check for updates
            </Button>
            <Button
              size="sm"
              onClick={() => {
                setEditing(undefined);
                setEditorOpen(true);
              }}
            >
              <Plus size={13} />
              Add
            </Button>
          </div>
        }
      >
        {loading ? (
          <p className="py-6 text-center text-sm text-muted-foreground">Loading…</p>
        ) : scripts.length === 0 ? (
          <p className="py-6 text-center text-sm text-muted-foreground">
            No userscripts yet. Click <strong>Add</strong> to paste or install one.
          </p>
        ) : (
          <ul className="grid gap-1.5">
            {scripts.map((script, index) => {
              const runAt = parseMetadata(script.source).runAt;
              return (
                <li
                  key={script.id}
                  className="flex items-center gap-2 rounded-md border bg-card px-2.5 py-2"
                >
                  <div className="flex shrink-0 flex-col">
                    <button
                      className="text-muted-foreground hover:text-foreground disabled:opacity-30"
                      onClick={() => void move(index, -1)}
                      disabled={busy || index === 0}
                      title="Move up"
                    >
                      <ChevronUp size={14} />
                    </button>
                    <button
                      className="text-muted-foreground hover:text-foreground disabled:opacity-30"
                      onClick={() => void move(index, 1)}
                      disabled={busy || index === scripts.length - 1}
                      title="Move down"
                    >
                      <ChevronDown size={14} />
                    </button>
                  </div>

                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2">
                      <span className="truncate text-sm font-medium">{script.name}</span>
                      {script.builtin && (
                        <Badge variant="outline" className="shrink-0">
                          Default
                        </Badge>
                      )}
                      {script.version && (
                        <span className="shrink-0 text-xs text-muted-foreground">
                          v{script.version}
                        </span>
                      )}
                      <Badge variant="secondary" className="shrink-0">
                        {runAt}
                      </Badge>
                      {script.pendingUpdate && (
                        <button
                          className="shrink-0"
                          onClick={() => setReviewing(script)}
                          title="Review update"
                        >
                          <Badge className="cursor-pointer bg-amber-500 hover:bg-amber-500/90">
                            Update v{script.pendingUpdate.version}
                          </Badge>
                        </button>
                      )}
                    </div>
                    <div className="mt-1 flex items-center gap-3 text-xs text-muted-foreground">
                      <label className="flex items-center gap-1.5">
                        <Switch
                          checked={script.enabled}
                          onCheckedChange={(v) => void toggle(script, { enabled: v })}
                        />
                        Enabled
                      </label>
                      <label className="flex items-center gap-1.5">
                        <Switch
                          checked={script.autoUpdate}
                          onCheckedChange={(v) => void toggle(script, { autoUpdate: v })}
                        />
                        Auto-update
                      </label>
                    </div>
                  </div>

                  <div className="flex shrink-0 items-center gap-1">
                    {script.pendingUpdate && (
                      <Button
                        variant="outline"
                        size="sm"
                        onClick={() => setReviewing(script)}
                        title="Review pending update"
                      >
                        <Download size={13} />
                        Review
                      </Button>
                    )}
                    <Button
                      variant="ghost"
                      size="icon"
                      onClick={() => {
                        setEditing(script);
                        setEditorOpen(true);
                      }}
                      title="Edit"
                    >
                      <Pencil size={14} />
                    </Button>
                    {!script.builtin && (
                      <Button
                        variant="ghost"
                        size="icon"
                        onClick={() => void remove(script)}
                        title="Delete"
                      >
                        <Trash2 size={14} />
                      </Button>
                    )}
                  </div>
                </li>
              );
            })}
          </ul>
        )}
        {(error || message) && (
          <p className={`mt-2 text-xs ${error ? "text-destructive" : "text-muted-foreground"}`}>
            {error || message}
          </p>
        )}
      </Panel>

      <Panel title="Updates">
        <div className="flex items-end gap-2">
          <div className="grid gap-1">
            <label className="text-xs text-muted-foreground">Check interval (hours, 0 = off)</label>
            <Input
              type="number"
              min={0}
              className="w-28"
              value={intervalHours}
              onChange={(e) => setIntervalHours(Number(e.target.value))}
            />
          </div>
          <Button variant="outline" size="sm" onClick={() => void saveInterval()} disabled={busy}>
            Save
          </Button>
          <p className="pb-1.5 text-xs text-muted-foreground">
            The background checker only records updates; they apply when you review and confirm.
          </p>
        </div>
      </Panel>

      <Panel title="Menu commands">
        {menuCommands.length === 0 ? (
          <p className="text-xs text-muted-foreground">
            No commands registered. Scripts add these via <code>GM_registerMenuCommand</code> while
            running.
          </p>
        ) : (
          <ul className="grid gap-1.5">
            {menuCommands.map((cmd) => (
              <li
                key={cmd.id}
                className="flex items-center justify-between gap-2 rounded-md border bg-card px-2.5 py-1.5"
              >
                <div className="min-w-0">
                  <span className="truncate text-sm">{cmd.label}</span>
                  <span className="ml-2 text-xs text-muted-foreground">{cmd.scriptName}</span>
                </div>
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => {
                    try {
                      cmd.callback();
                    } catch (err) {
                      console.error("menu command failed", err);
                    }
                  }}
                >
                  Run
                </Button>
              </li>
            ))}
          </ul>
        )}
      </Panel>

      {editorOpen && (
        <UserscriptEditor
          key={editing?.id ?? "new"}
          initial={editing}
          onSave={saveScript}
          onClose={() => setEditorOpen(false)}
        />
      )}
      {reviewing && (
        <PendingUpdateDialog
          script={reviewing}
          onApply={() => applyUpdate(reviewing)}
          onDismiss={() => dismissUpdate(reviewing)}
          onClose={() => setReviewing(undefined)}
        />
      )}
    </div>
  );
}
