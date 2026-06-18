import { ArrowDown, ArrowUp, Plus, RotateCcw, Save, Trash2 } from "lucide-react";
import { useEffect, useState } from "react";
import { getOptimizationSettings, saveOptimizationSettings } from "@/api";
import {
  applySettings,
  getOptimizationStatus,
  type OptimizationStatus,
} from "@/optimizations/state";
import { PerformanceStats } from "@/components/tabs/PerformanceStats";
import { Panel } from "@/components/shared/Panel";
import { TabPanel } from "@/components/ui/tab-panel";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { cn, errorMessage } from "@/lib/utils";
import {
  type OptimizationFeatureKey,
  type OptimizationFeatures,
  type OptimizationProfile,
  type OptimizationRule,
  type OptimizationSettings,
  type OptimizationTrigger,
} from "@/types";

const FEATURES: { key: OptimizationFeatureKey; label: string; hint: string }[] = [
  { key: "lazyCanvas", label: "Lazy canvas", hint: "Skip redundant character canvas rebuilds" },
  {
    key: "idleFpsThrottle",
    label: "Idle FPS throttle",
    hint: "Pace the chat-room redraw to ~5fps",
  },
  { key: "skipValidation", label: "Skip validation", hint: "Bypass appearance sanitization" },
  { key: "chatLogTrim", label: "Trim chat log", hint: "Cap the in-DOM chat log length" },
  {
    key: "tintCache",
    label: "Tint cache",
    hint: "Cache the per-character blind/tint overlay",
  },
];

const TRIGGERS: { value: OptimizationTrigger; label: string }[] = [
  { value: "background", label: "Background (tab hidden)" },
  { value: "idle", label: "Idle (no input)" },
  { value: "default", label: "Default (always)" },
];

function emptyFeatures(): OptimizationFeatures {
  return {
    lazyCanvas: false,
    idleFpsThrottle: false,
    skipValidation: false,
    chatLogTrim: false,
    tintCache: false,
  };
}

function newProfileId(): string {
  return typeof crypto !== "undefined" && crypto.randomUUID
    ? crypto.randomUUID().slice(0, 8)
    : `p${Math.floor(Math.random() * 1e9)}`;
}

export function OptimizationsTab() {
  const [settings, setSettings] = useState<OptimizationSettings | null>(null);
  // JSON of the last loaded/saved config, for dirty detection.
  const [savedSnapshot, setSavedSnapshot] = useState("");
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");
  // Live loader status, polled so the config view can flag the rule in effect now.
  const [status, setStatus] = useState<OptimizationStatus | null>(null);

  useEffect(() => {
    void reload();
  }, []);

  useEffect(() => {
    const tick = () => setStatus(getOptimizationStatus());
    tick();
    const timer = window.setInterval(tick, 1000);
    return () => window.clearInterval(timer);
  }, []);

  const dirty = settings != null && JSON.stringify(settings) !== savedSnapshot;

  async function reload() {
    setLoading(true);
    setError("");
    setMessage("");
    try {
      const next = await getOptimizationSettings();
      setSettings(next);
      setSavedSnapshot(JSON.stringify(next));
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setLoading(false);
    }
  }

  async function save() {
    if (!settings) return;
    setBusy(true);
    setError("");
    setMessage("");
    try {
      const saved = await saveOptimizationSettings(settings);
      setSettings(saved);
      setSavedSnapshot(JSON.stringify(saved));
      // Apply live to the loader (same module graph, shared singletons) — no reload.
      applySettings(saved);
      setMessage("Saved.");
    } catch (err) {
      setError(errorMessage(err));
    } finally {
      setBusy(false);
    }
  }

  // --- profile mutations -----------------------------------------------------
  function patchProfile(id: string, patch: Partial<OptimizationProfile>) {
    setSettings((s) =>
      s ? { ...s, profiles: s.profiles.map((p) => (p.id === id ? { ...p, ...patch } : p)) } : s,
    );
  }
  function setFeature(id: string, key: OptimizationFeatureKey, value: boolean) {
    setSettings((s) =>
      s
        ? {
            ...s,
            profiles: s.profiles.map((p) =>
              p.id === id ? { ...p, features: { ...p.features, [key]: value } } : p,
            ),
          }
        : s,
    );
  }
  function addProfile() {
    setSettings((s) =>
      s
        ? {
            ...s,
            profiles: [
              ...s.profiles,
              {
                id: newProfileId(),
                name: `Profile ${s.profiles.length + 1}`,
                features: emptyFeatures(),
              },
            ],
          }
        : s,
    );
  }
  function removeProfile(id: string) {
    setSettings((s) => (s ? { ...s, profiles: s.profiles.filter((p) => p.id !== id) } : s));
  }

  // --- rule mutations --------------------------------------------------------
  function patchRule(index: number, patch: Partial<OptimizationRule>) {
    setSettings((s) =>
      s ? { ...s, rules: s.rules.map((r, i) => (i === index ? { ...r, ...patch } : r)) } : s,
    );
  }
  function addRule() {
    setSettings((s) =>
      s
        ? {
            ...s,
            rules: [
              ...s.rules,
              { trigger: "default", profile: s.profiles[0]?.id ?? "" } as OptimizationRule,
            ],
          }
        : s,
    );
  }
  function removeRule(index: number) {
    setSettings((s) => (s ? { ...s, rules: s.rules.filter((_, i) => i !== index) } : s));
  }
  function moveRule(index: number, dir: -1 | 1) {
    setSettings((s) => {
      if (!s) return s;
      const target = index + dir;
      if (target < 0 || target >= s.rules.length) return s;
      const rules = [...s.rules];
      [rules[index], rules[target]] = [rules[target], rules[index]];
      return { ...s, rules };
    });
  }

  if (loading || !settings) {
    return <p className="py-8 text-center text-sm text-muted-foreground">Loading…</p>;
  }

  const liveRuleIndex =
    !dirty && status?.enabled && status.activeProfileId
      ? settings.rules.findIndex(
          (r) => r.trigger === status.activeTrigger && r.profile === status.activeProfileId,
        )
      : -1;

  return (
    <TabPanel defaultValue="stats" className="@container max-w-3xl">
      <TabPanel.Entry value="stats" name="Stats">
        <PerformanceStats />
      </TabPanel.Entry>

      <TabPanel.Entry
        value="config"
        name={
          <span className="inline-flex items-center">
            Config
            {dirty && (
              <span className="ml-1.5 inline-block h-1.5 w-1.5 rounded-full bg-amber-500" />
            )}
          </span>
        }
      >
        <div className="flex items-center justify-end gap-2">
          <Button size="sm" variant="ghost" onClick={() => void reload()} disabled={busy || !dirty}>
            <RotateCcw size={14} className="mr-1" /> Revert
          </Button>
          <Button size="sm" onClick={() => void save()} disabled={busy || !dirty}>
            <Save size={14} className="mr-1" /> Save
          </Button>
        </div>

        <Panel title="Master switch">
          <label className="flex items-center gap-2 text-sm">
            <Switch
              checked={settings.enabled}
              onCheckedChange={(v) => setSettings((s) => (s ? { ...s, enabled: v } : s))}
            />
            <span>
              Enable optimizations
              <span className="ml-1 text-xs text-muted-foreground">
                — when off, every hook passes through and the game is untouched.
              </span>
            </span>
          </label>
        </Panel>

        <Panel
          title="Profiles"
          action={
            <Button size="sm" variant="ghost" onClick={addProfile}>
              <Plus size={14} className="mr-1" /> Add
            </Button>
          }
        >
          <div className="grid gap-3">
            {settings.profiles.length === 0 && (
              <p className="text-xs text-muted-foreground">
                No profiles yet. Add one to get started.
              </p>
            )}
            {settings.profiles.map((profile) => {
              const live = !dirty && status?.activeProfileId === profile.id;
              return (
                <div
                  key={profile.id}
                  className={cn(
                    "rounded-md border bg-background p-2.5",
                    live && "border-primary/60 ring-1 ring-primary/30",
                  )}
                >
                  <div className="mb-2 flex items-center gap-2">
                    <Input
                      value={profile.name}
                      onChange={(e) => patchProfile(profile.id, { name: e.target.value })}
                      className="h-7 max-w-48"
                      aria-label="Profile name"
                    />
                    {live && (
                      <span className="flex items-center gap-1 text-[11px] font-medium text-primary">
                        <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-primary" />
                        live
                      </span>
                    )}
                    <Button
                      size="icon"
                      variant="ghost"
                      className="ml-auto h-7 w-7 text-muted-foreground hover:text-destructive"
                      onClick={() => removeProfile(profile.id)}
                      aria-label="Remove profile"
                    >
                      <Trash2 size={14} />
                    </Button>
                  </div>
                  <div className="grid gap-1.5 @md:grid-cols-2">
                    {FEATURES.map((f) => (
                      <label key={f.key} className="flex items-center gap-2 text-sm" title={f.hint}>
                        <Switch
                          checked={profile.features[f.key]}
                          onCheckedChange={(v) => setFeature(profile.id, f.key, v)}
                        />
                        <span>{f.label}</span>
                      </label>
                    ))}
                  </div>
                </div>
              );
            })}
          </div>
        </Panel>

        <Panel
          title="Rules"
          action={
            <Button size="sm" variant="ghost" onClick={addRule}>
              <Plus size={14} className="mr-1" /> Add
            </Button>
          }
        >
          <p className="mb-2 text-xs text-muted-foreground">
            Evaluated top to bottom; the first matching rule selects the active profile. Keep a{" "}
            <span className="font-medium">Default</span> rule last as the fallback.
          </p>
          <div className="grid gap-2">
            {settings.rules.map((rule, i) => (
              <div
                key={i}
                className={cn(
                  "flex flex-wrap items-center gap-2 rounded-md border bg-background p-2",
                  i === liveRuleIndex && "border-primary/60 ring-1 ring-primary/30",
                )}
              >
                <span className="w-4 text-center text-xs text-muted-foreground">{i + 1}</span>
                <Select
                  value={rule.trigger}
                  onValueChange={(v) => patchRule(i, { trigger: v as OptimizationTrigger })}
                >
                  <SelectTrigger className="h-7 w-52">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {TRIGGERS.map((t) => (
                      <SelectItem key={t.value} value={t.value}>
                        {t.label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                {rule.trigger === "idle" && (
                  <div className="flex items-center gap-1 text-xs text-muted-foreground">
                    <Input
                      type="number"
                      min={1}
                      value={rule.idleSeconds ?? 30}
                      onChange={(e) =>
                        patchRule(i, { idleSeconds: Math.max(1, Number(e.target.value) || 0) })
                      }
                      className="h-7 w-16"
                      aria-label="Idle seconds"
                    />
                    s
                  </div>
                )}
                <span className="text-xs text-muted-foreground">→</span>
                <Select value={rule.profile} onValueChange={(v) => patchRule(i, { profile: v })}>
                  <SelectTrigger className="h-7 w-40">
                    <SelectValue placeholder="Profile…" />
                  </SelectTrigger>
                  <SelectContent>
                    {settings.profiles.map((p) => (
                      <SelectItem key={p.id} value={p.id}>
                        {p.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                {i === liveRuleIndex && (
                  <span className="flex items-center gap-1 text-[11px] font-medium text-primary">
                    <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-primary" />
                    live
                  </span>
                )}
                <div className="ml-auto flex items-center">
                  <Button
                    size="icon"
                    variant="ghost"
                    className="h-7 w-7"
                    onClick={() => moveRule(i, -1)}
                    disabled={i === 0}
                    aria-label="Move up"
                  >
                    <ArrowUp size={14} />
                  </Button>
                  <Button
                    size="icon"
                    variant="ghost"
                    className="h-7 w-7"
                    onClick={() => moveRule(i, 1)}
                    disabled={i === settings.rules.length - 1}
                    aria-label="Move down"
                  >
                    <ArrowDown size={14} />
                  </Button>
                  <Button
                    size="icon"
                    variant="ghost"
                    className="h-7 w-7 text-muted-foreground hover:text-destructive"
                    onClick={() => removeRule(i)}
                    aria-label="Remove rule"
                  >
                    <Trash2 size={14} />
                  </Button>
                </div>
              </div>
            ))}
            {settings.rules.length === 0 && (
              <p className="text-xs text-muted-foreground">No rules — the game stays untouched.</p>
            )}
          </div>
        </Panel>

        {(error || message) && (
          <p className={`text-xs ${error ? "text-destructive" : "text-muted-foreground"}`}>
            {error || message}
          </p>
        )}
      </TabPanel.Entry>
    </TabPanel>
  );
}
