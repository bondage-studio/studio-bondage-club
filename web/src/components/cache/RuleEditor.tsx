import { useState } from "react";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Label } from "../ui/label";
import { Switch } from "../ui/switch";
import { Window } from "../ui/window";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "../ui/select";
import type { CacheRule, StoreConfig } from "../../types";

interface RuleEditorProps {
  initial?: CacheRule;
  stores: StoreConfig[];
  onSave: (rule: CacheRule) => void;
  onClose: () => void;
}

const empty: CacheRule = {};

export function RuleEditor({ initial, stores, onSave, onClose }: RuleEditorProps) {
  const isEdit = !!initial;
  const [form, setForm] = useState<CacheRule>(initial ?? empty);

  function set(patch: Partial<CacheRule>) {
    setForm((f) => ({ ...f, ...patch }));
  }

  const storeOptions = [{ name: "default" }, ...stores];

  return (
    <Window onClose={onClose} defaultWidth={460} defaultHeight={600} minWidth={380} minHeight={400}>
      <Window.Title>{isEdit ? "Edit rule" : "Add rule"}</Window.Title>

      <Window.Body className="overflow-y-auto p-4">
        <div className="grid gap-3">
          <p className="text-xs text-muted-foreground">
            Conditions — all non-empty fields must match. Leave blank to match all.
          </p>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-host">Host</Label>
            <Input
              id="rule-host"
              value={form.host ?? ""}
              onChange={(e) => set({ host: e.target.value || undefined })}
              placeholder="bondage-europe.com"
              spellCheck={false}
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-prefix">Path prefix</Label>
            <Input
              id="rule-prefix"
              value={form.pathPrefix ?? ""}
              onChange={(e) => set({ pathPrefix: e.target.value || undefined })}
              placeholder="/Assets/"
              spellCheck={false}
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-pattern">
              Path pattern <span className="text-muted-foreground">(glob or re:…)</span>
            </Label>
            <Input
              id="rule-pattern"
              value={form.pathPattern ?? ""}
              onChange={(e) => set({ pathPattern: e.target.value || undefined })}
              placeholder="*.js  or  re:^/Assets/.*\.png$"
              spellCheck={false}
            />
          </div>

          <div className="border-t pt-3">
            <p className="text-xs text-muted-foreground mb-3">Actions</p>
          </div>

          <div className="grid grid-cols-2 gap-2">
            <div className="grid gap-1.5">
              <Label>Store</Label>
              <Select
                value={form.store ?? "default"}
                onValueChange={(v) => set({ store: v === "default" ? undefined : v })}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  {storeOptions.map((s) => (
                    <SelectItem key={s.name} value={s.name}>
                      {s.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
            <div className="grid gap-1.5">
              <Label>Key mode</Label>
              <Select
                value={form.keyMode ?? "url"}
                onValueChange={(v) => set({ keyMode: v === "url" ? undefined : (v as "path") })}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="url">url (default)</SelectItem>
                  <SelectItem value="path">path (portable)</SelectItem>
                </SelectContent>
              </Select>
            </div>
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-ttl">TTL (seconds)</Label>
            <Input
              id="rule-ttl"
              type="number"
              min={0}
              value={form.ttlSeconds ?? ""}
              onChange={(e) =>
                set({ ttlSeconds: e.target.value ? Number(e.target.value) : undefined })
              }
              placeholder="leave empty for store default"
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-cc">Cache-Control override</Label>
            <Input
              id="rule-cc"
              value={form.cacheControl ?? ""}
              onChange={(e) => set({ cacheControl: e.target.value || undefined })}
              placeholder="public, max-age=31536000, immutable"
              spellCheck={false}
            />
          </div>

          <div className="border-t pt-3">
            <p className="text-xs text-muted-foreground mb-1">Version-aware caching (optional)</p>
            <p className="text-xs text-muted-foreground">
              Tag entries with a source version so a version bump revalidates via ETag instead of
              serving stale bytes. A key rewrite collapses different URL shapes onto one entry.
            </p>
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-version">
              Version <span className="text-muted-foreground">(query:&lt;name&gt; or re:…)</span>
            </Label>
            <Input
              id="rule-version"
              value={form.version ?? ""}
              onChange={(e) => set({ version: e.target.value || undefined })}
              placeholder="re:/(R\d+)/BondageClub/   or   query:v"
              spellCheck={false}
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-keypattern">
              Key pattern <span className="text-muted-foreground">(re:…)</span>
            </Label>
            <Input
              id="rule-keypattern"
              value={form.keyPattern ?? ""}
              onChange={(e) => set({ keyPattern: e.target.value || undefined })}
              placeholder="re:^/(echo-(?:clothing|activity)-ext)/(.*)$"
              spellCheck={false}
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="rule-keytemplate">
              Key template <span className="text-muted-foreground">($1-style refs)</span>
            </Label>
            <Input
              id="rule-keytemplate"
              value={form.keyTemplate ?? ""}
              onChange={(e) => set({ keyTemplate: e.target.value || undefined })}
              placeholder="$1/$2"
              spellCheck={false}
            />
          </div>

          <div className="flex items-center justify-between rounded-md border px-3 py-2">
            <div>
              <p className="text-sm font-medium">Revalidate on version change</p>
              <p className="text-xs text-muted-foreground">
                On: version-independent key, ETag revalidate across bumps (game body). Off:
                immutable — version is part of the key (commit SHA / ?v=).
              </p>
            </div>
            <Switch
              checked={!!form.versionRevalidate}
              onCheckedChange={(v) => set({ versionRevalidate: v || undefined })}
            />
          </div>

          <div className="flex items-center justify-between rounded-md border px-3 py-2">
            <div>
              <p className="text-sm font-medium">Force cache</p>
              <p className="text-xs text-muted-foreground">
                Ignore upstream Cache-Control: no-store
              </p>
            </div>
            <Switch
              checked={!!form.forceCache}
              onCheckedChange={(v) => set({ forceCache: v || undefined })}
            />
          </div>

          <div className="flex items-center justify-between rounded-md border px-3 py-2">
            <div>
              <p className="text-sm font-medium">Bypass cache</p>
              <p className="text-xs text-muted-foreground">Always proxy through to upstream</p>
            </div>
            <Switch
              checked={!!form.bypass}
              onCheckedChange={(v) => set({ bypass: v || undefined })}
            />
          </div>
        </div>
      </Window.Body>

      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={onClose}>
            Cancel
          </Button>
          <Button size="sm" onClick={() => onSave(form)}>
            {isEdit ? "Update" : "Add"}
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}
