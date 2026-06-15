import { useMemo, useState } from "react";
import { Button } from "@/components/ui/button";
import { CodeEditor } from "@/components/ui/code-editor";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { Window } from "@/components/ui/window";
import { parseMetadata, toProxyURL } from "@/userscripts/metadata";
import { errorMessage } from "@/lib/utils";
import type { Userscript } from "@/types";

interface Props {
  initial?: Userscript;
  onSave: (script: Userscript) => Promise<void> | void;
  onClose: () => void;
}

function newId(): string {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) return crypto.randomUUID();
  return `us-${Date.now()}-${Math.random().toString(36).slice(2)}`;
}

export function UserscriptEditor({ initial, onSave, onClose }: Props) {
  const isEdit = !!initial;
  const isBuiltin = !!initial?.builtin;
  const [source, setSource] = useState(initial?.source ?? "");
  const [enabled, setEnabled] = useState(initial?.enabled ?? true);
  const [autoUpdate, setAutoUpdate] = useState(initial?.autoUpdate ?? false);
  const [installUrl, setInstallUrl] = useState(initial?.downloadURL ?? initial?.updateURL ?? "");
  const [installing, setInstalling] = useState(false);
  const [error, setError] = useState("");
  const [saving, setSaving] = useState(false);

  // Parse metadata live for the preview. The full source is always shown for
  // review — saving is an explicit action, never silent.
  const meta = useMemo(() => parseMetadata(source), [source]);

  async function handleInstall() {
    if (!installUrl.trim()) return;
    setInstalling(true);
    setError("");
    try {
      const res = await fetch(toProxyURL(installUrl.trim()), { credentials: "omit" });
      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
      setSource(await res.text());
    } catch (err) {
      setError(`Fetch failed: ${errorMessage(err)}`);
    } finally {
      setInstalling(false);
    }
  }

  async function handleSave() {
    if (!source.trim()) {
      setError("Script source is empty.");
      return;
    }
    setSaving(true);
    setError("");
    const script: Userscript = {
      id: initial?.id ?? newId(),
      name: meta.name || initial?.name || "Untitled script",
      source,
      enabled,
      autoUpdate,
      downloadURL: installUrl.trim() || meta.downloadURL || initial?.downloadURL,
      updateURL: meta.updateURL || initial?.updateURL,
      version: meta.version || initial?.version,
      sortOrder: initial?.sortOrder,
    };
    try {
      await onSave(script);
      onClose();
    } catch (err) {
      setError(errorMessage(err));
      setSaving(false);
    }
  }

  return (
    <Window onClose={onClose} defaultWidth={640} defaultHeight={620} minWidth={460} minHeight={420}>
      <Window.Title>{isEdit ? "Edit userscript" : "Add userscript"}</Window.Title>

      <Window.Body className="overflow-y-auto p-4">
        <div className="grid gap-3">
          <div className="grid gap-1.5">
            <Label htmlFor="us-install-url">Source URL (for updates)</Label>
            <div className="flex gap-2">
              <Input
                id="us-install-url"
                value={installUrl}
                onChange={(e) => setInstallUrl(e.target.value)}
                placeholder="https://example.com/script.user.js"
                spellCheck={false}
                autoComplete="off"
                readOnly={isBuiltin}
                disabled={isBuiltin}
              />
              <Button
                variant="outline"
                size="sm"
                onClick={() => void handleInstall()}
                disabled={installing || !installUrl.trim()}
              >
                {installing ? "Fetching…" : "Fetch"}
              </Button>
            </div>
            <p className="text-xs text-muted-foreground">
              {isBuiltin
                ? "This is a default script — its source URL and name are fixed. You can still edit the source, toggle it, and pull updates."
                : "Used to check for and download updates."}{" "}
              <strong>Fetch</strong> pulls the latest source into the editor for review — nothing is
              saved until you click {isEdit ? "Update" : "Add"}.
            </p>
          </div>

          <div className="grid gap-1.5">
            <Label>Source</Label>
            <CodeEditor
              value={source}
              onChange={setSource}
              autoFocus={!isEdit}
              placeholder={
                "// ==UserScript==\n// @name Example\n// @run-at document-end\n// ==/UserScript=="
              }
              className="h-80"
            />
          </div>

          <div className="rounded-md border bg-muted/40 p-2.5 text-xs">
            <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
              <span className="text-muted-foreground">Name</span>
              <span className="font-medium">{meta.name || "—"}</span>
              <span className="text-muted-foreground">Version</span>
              <span>{meta.version || "—"}</span>
              <span className="text-muted-foreground">Run at</span>
              <span>{meta.runAt}</span>
              {meta.grant.length > 0 && (
                <>
                  <span className="text-muted-foreground">Grants</span>
                  <span className="break-words">{meta.grant.join(", ")}</span>
                </>
              )}
              {meta.require.length > 0 && (
                <>
                  <span className="text-muted-foreground">Requires</span>
                  <span className="break-all">{meta.require.length} library(ies)</span>
                </>
              )}
            </div>
          </div>

          <div className="flex items-center justify-between rounded-md border p-2.5">
            <div>
              <p className="text-sm font-medium">Enabled</p>
              <p className="text-xs text-muted-foreground">Run this script on the game page.</p>
            </div>
            <Switch checked={enabled} onCheckedChange={setEnabled} />
          </div>

          <div className="flex items-center justify-between rounded-md border p-2.5">
            <div>
              <p className="text-sm font-medium">Auto-update</p>
              <p className="text-xs text-muted-foreground">
                Check the update/download URL for newer versions in the background. Updates are
                never applied without your confirmation.
              </p>
            </div>
            <Switch checked={autoUpdate} onCheckedChange={setAutoUpdate} />
          </div>

          {error && <p className="text-xs text-destructive">{error}</p>}
        </div>
      </Window.Body>

      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={onClose}>
            Cancel
          </Button>
          <Button size="sm" onClick={() => void handleSave()} disabled={saving || !source.trim()}>
            {isEdit ? "Update" : "Add"}
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}
