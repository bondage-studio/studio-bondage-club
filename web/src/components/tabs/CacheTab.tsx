import { Pencil, Plus, Trash2 } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { FormField } from "@/components/shared/FormField";
import { Panel } from "@/components/shared/Panel";
import { TabPanel } from "@/components/ui/tab-panel";
import { StoreCard } from "@/components/cache/StoreCard";
import { CacheTrafficPanel } from "@/components/cache/CacheTrafficPanel";
import { StatusCodesField } from "@/components/cache/StatusCodesField";
import { CacheMaintenance } from "@/components/cache/CacheMaintenance";
import { formatBytes } from "@/lib/utils";
import { IS_ANDROID_BUILD } from "@/lib/platform";
import type { AppConfig, StatsEvent } from "@/types";

interface Props {
  form: AppConfig;
  connected: boolean;
  sseStats: StatsEvent | null;
  busy: boolean;
  onChange: (mutator: (draft: AppConfig) => void) => void;
  onClearCache: () => void;
  onAddStore: () => void;
  onEditStore: (idx: number) => void;
  onDeleteStore: (idx: number) => void;
  onAddRule: () => void;
  onEditRule: (idx: number) => void;
  onDeleteRule: (idx: number) => void;
  onMoveRule: (idx: number, dir: -1 | 1) => void;
}

export function CacheTab({
  form,
  connected,
  sseStats,
  busy,
  onChange,
  onClearCache,
  onAddStore,
  onEditStore,
  onDeleteStore,
  onAddRule,
  onEditRule,
  onDeleteRule,
  onMoveRule,
}: Props) {
  const stores = form.cache.stores ?? [];
  const rules = form.cache.rules ?? [];

  return (
    <TabPanel defaultValue="stats">
      <TabPanel.Entry value="stats" name="Statistics">
        <Panel
          title="Live statistics"
          action={
            <div className="flex items-center gap-1.5">
              <Badge variant={connected ? "success" : "outline"} className="text-xs">
                {connected ? "● Live" : "○ Offline"}
              </Badge>
              <Button
                variant="ghost"
                size="sm"
                onClick={onClearCache}
                disabled={busy}
                className="h-6 px-2 text-destructive hover:bg-destructive/10 hover:text-destructive"
              >
                <Trash2 size={12} />
                Clear
              </Button>
            </div>
          }
        >
          {sseStats ? (
            <div className="grid grid-cols-2 gap-2">
              {sseStats.stores.map((s) => (
                <StoreCard key={s.name} store={s} />
              ))}
              {sseStats.stores.length > 1 && (
                <div className="col-span-2 flex gap-4 px-1 text-xs text-muted-foreground">
                  <span>
                    Total:{" "}
                    <strong className="text-foreground">
                      {sseStats.total.entries.toLocaleString()}
                    </strong>{" "}
                    entries,{" "}
                    <strong className="text-foreground">{formatBytes(sseStats.total.bytes)}</strong>
                  </span>
                </div>
              )}
            </div>
          ) : (
            <p className="text-xs text-muted-foreground">Waiting for data…</p>
          )}
        </Panel>

        <CacheTrafficPanel traffic={sseStats?.traffic} />
      </TabPanel.Entry>

      <TabPanel.Entry value="config" name="Configuration">
        <Panel title="Base configuration">
          <div className="grid gap-3">
            {/* The Android app manages cache storage in an app-internal path. */}
            {!IS_ANDROID_BUILD && (
              <FormField label="Cache directory">
                <Input
                  value={form.cache.dir}
                  onChange={(e) => onChange((d) => void (d.cache.dir = e.target.value))}
                  spellCheck={false}
                />
              </FormField>
            )}
            <div className="grid grid-cols-2 gap-3">
              <FormField label="Default TTL (s)">
                <Input
                  type="number"
                  min={0}
                  value={form.cache.defaultTTLSeconds}
                  onChange={(e) =>
                    onChange((d) => void (d.cache.defaultTTLSeconds = Number(e.target.value)))
                  }
                />
              </FormField>
              <FormField label="Max bytes">
                <Input
                  type="number"
                  min={0}
                  value={form.cache.maxSizeBytes}
                  onChange={(e) =>
                    onChange((d) => void (d.cache.maxSizeBytes = Number(e.target.value)))
                  }
                />
              </FormField>
            </div>
            <FormField label="Cacheable status codes">
              <StatusCodesField
                value={form.cache.cacheableStatusCodes}
                onChange={(codes) => onChange((d) => void (d.cache.cacheableStatusCodes = codes))}
              />
              <p className="mt-1 text-[11px] text-muted-foreground">
                Space- or comma-separated HTTP status codes to cache. Leave empty for the default
                (200 204 404) — caching 404s stops repeated upstream probes for missing assets.
              </p>
            </FormField>
          </div>
        </Panel>

        <Panel
          title="Named stores"
          action={
            <Button variant="outline" size="sm" className="h-6 px-2" onClick={onAddStore}>
              <Plus size={12} />
              Add
            </Button>
          }
        >
          {stores.length === 0 ? (
            <p className="text-xs text-muted-foreground">
              No named stores — all traffic uses "default".
            </p>
          ) : (
            <div className="grid gap-1.5">
              {stores.map((store, i) => (
                <div
                  key={store.name}
                  className="flex items-center justify-between rounded-md border px-3 py-1.5 text-sm"
                >
                  <div className="flex items-center gap-2">
                    <span className="font-medium">{store.name}</span>
                    {store.maxSizeBytes ? (
                      <Badge variant="secondary">{formatBytes(store.maxSizeBytes)}</Badge>
                    ) : null}
                  </div>
                  <div className="flex gap-1">
                    <Button
                      variant="ghost"
                      size="icon"
                      className="h-6 w-6"
                      onClick={() => onEditStore(i)}
                    >
                      <Pencil size={12} />
                    </Button>
                    <Button
                      variant="ghost"
                      size="icon"
                      className="h-6 w-6 text-destructive hover:text-destructive"
                      onClick={() => onDeleteStore(i)}
                    >
                      <Trash2 size={12} />
                    </Button>
                  </div>
                </div>
              ))}
            </div>
          )}
        </Panel>

        <Panel
          title="Policy rules"
          action={
            <Button variant="outline" size="sm" className="h-6 px-2" onClick={onAddRule}>
              <Plus size={12} />
              Add
            </Button>
          }
        >
          {rules.length === 0 ? (
            <p className="text-xs text-muted-foreground">
              No rules — all requests use default settings.
            </p>
          ) : (
            <div className="grid gap-1.5">
              {rules.map((rule, i) => (
                <div key={i} className="rounded-md border px-3 py-2 text-xs">
                  <div className="flex items-start justify-between gap-2">
                    <div className="flex min-w-0 flex-wrap gap-1">
                      {rule.host && <Badge variant="outline">host:{rule.host}</Badge>}
                      {rule.pathPrefix && <Badge variant="outline">prefix:{rule.pathPrefix}</Badge>}
                      {rule.pathPattern && (
                        <Badge variant="outline">pattern:{rule.pathPattern}</Badge>
                      )}
                      {!rule.host && !rule.pathPrefix && !rule.pathPattern && (
                        <Badge variant="secondary">match all</Badge>
                      )}
                      {rule.bypass && <Badge variant="destructive">bypass</Badge>}
                      {rule.forceCache && <Badge variant="default">force</Badge>}
                      {rule.keyMode === "path" && <Badge variant="secondary">key:path</Badge>}
                      {rule.version && <Badge variant="outline">ver:{rule.version}</Badge>}
                      {rule.keyPattern && <Badge variant="outline">key-rewrite</Badge>}
                      {rule.cacheableStatusCodes && rule.cacheableStatusCodes.length > 0 && (
                        <Badge variant="outline">codes:{rule.cacheableStatusCodes.join(",")}</Badge>
                      )}
                      {rule.store && (
                        <Badge variant="secondary">
                          {">"}
                          {rule.store}
                        </Badge>
                      )}
                    </div>
                    <div className="flex shrink-0 gap-1">
                      <Button
                        variant="ghost"
                        size="icon"
                        className="h-5 w-5"
                        disabled={i === 0}
                        onClick={() => onMoveRule(i, -1)}
                        title="Move up"
                      >
                        ↑
                      </Button>
                      <Button
                        variant="ghost"
                        size="icon"
                        className="h-5 w-5"
                        disabled={i === rules.length - 1}
                        onClick={() => onMoveRule(i, 1)}
                        title="Move down"
                      >
                        ↓
                      </Button>
                      <Button
                        variant="ghost"
                        size="icon"
                        className="h-5 w-5"
                        onClick={() => onEditRule(i)}
                      >
                        <Pencil size={10} />
                      </Button>
                      <Button
                        variant="ghost"
                        size="icon"
                        className="h-5 w-5 text-destructive hover:text-destructive"
                        onClick={() => onDeleteRule(i)}
                      >
                        <Trash2 size={10} />
                      </Button>
                    </div>
                  </div>
                  {rule.cacheControl && (
                    <p className="mt-1 truncate text-muted-foreground">cc: {rule.cacheControl}</p>
                  )}
                </div>
              ))}
            </div>
          )}
        </Panel>
      </TabPanel.Entry>

      <TabPanel.Entry value="maintenance" name="Maintenance">
        <CacheMaintenance />
      </TabPanel.Entry>
    </TabPanel>
  );
}
