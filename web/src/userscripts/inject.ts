// Orchestrates userscript injection into the rebuilt Bondage Club page. Hooked
// from originalPage.ts at three phases (document-start / -end / -idle). On the
// first call it fetches the enabled scripts, preloads each one's GM values, and
// fetches its @require libraries and @resource files (through the proxy); then
// each phase call executes the scripts whose @run-at matches.

import { getUserscriptValues, listUserscripts } from "@/api";
import type { Userscript } from "@/types";
import { buildGmBindings, type ResourceData } from "@/userscripts/gmApi";
import { parseMetadata, toProxyURL, type RunAt, type UserscriptMetadata } from "@/userscripts/metadata";
import { createValueStore } from "@/userscripts/values";

interface PreparedScript {
  script: Userscript;
  meta: UserscriptMetadata;
  requires: string[];
  bindings: Record<string, unknown>;
  executed: boolean;
}

declare global {
  interface Window {
    __studioUserscriptRegistry?: Record<string, Record<string, unknown>>;
  }
}

let preparedPromise: Promise<PreparedScript[]> | null = null;

async function fetchText(url: string): Promise<string> {
  const res = await fetch(toProxyURL(url), { credentials: "omit" });
  if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
  return res.text();
}

async function prepareScript(script: Userscript): Promise<PreparedScript> {
  const meta = parseMetadata(script.source);

  // Preload values BEFORE the script runs so GM_getValue is synchronous.
  let initialValues: Record<string, unknown> = {};
  try {
    initialValues = await getUserscriptValues(script.id);
  } catch (err) {
    console.error(`[userscript ${script.name}] failed to preload values`, err);
  }
  const values = createValueStore(script.id, initialValues);

  // @require libraries — fetched in parallel, order preserved. A failed require
  // becomes an empty string so the rest of the script still has a chance to run.
  const requires = await Promise.all(
    meta.require.map((url) =>
      fetchText(url).catch((err) => {
        console.error(`[userscript ${script.name}] @require ${url} failed`, err);
        return "";
      }),
    ),
  );

  // @resource files — text preloaded for GM_getResourceText; the URL is the
  // local proxy URL (usable directly as an <img>/<link> source).
  const resources: Record<string, ResourceData> = {};
  await Promise.all(
    meta.resource.map(async (r) => {
      let text = "";
      try {
        text = await fetchText(r.url);
      } catch (err) {
        console.error(`[userscript ${script.name}] @resource ${r.name} failed`, err);
      }
      resources[r.name] = { text, url: toProxyURL(r.url) };
    }),
  );

  const bindings = buildGmBindings(script, meta, values, resources);
  return { script, meta, requires, bindings, executed: false };
}

async function prepareAll(): Promise<PreparedScript[]> {
  let scripts: Userscript[] = [];
  try {
    scripts = await listUserscripts();
  } catch (err) {
    console.error("[userscripts] failed to list scripts", err);
    return [];
  }
  // The server returns scripts sorted by sortOrder; run only the enabled ones.
  const enabled = scripts.filter((s) => s.enabled);
  const prepared: PreparedScript[] = [];
  for (const script of enabled) {
    try {
      prepared.push(await prepareScript(script));
    } catch (err) {
      console.error(`[userscript ${script.name}] preparation failed`, err);
    }
  }
  return prepared;
}

function registry(): Record<string, Record<string, unknown>> {
  let reg = window.__studioUserscriptRegistry;
  if (!reg) {
    reg = {};
    window.__studioUserscriptRegistry = reg;
  }
  return reg;
}

// Execute one prepared script by injecting a blob-URL <script>. The script body
// pulls its GM bindings from the per-injection registry and binds them as local
// variables, then runs @require libraries followed by the user source — all in
// one closure (no eval/new Function, robust to any CSP on the cached page).
function executeScript(prepared: PreparedScript): Promise<void> {
  const token = `${prepared.script.id}:${Math.random().toString(36).slice(2)}`;
  registry()[token] = prepared.bindings;

  const names = Object.keys(prepared.bindings);
  const preamble = names.map((n) => `var ${n} = __b[${JSON.stringify(n)}];`).join("\n");
  const requireCode = prepared.requires.filter(Boolean).join("\n;\n");
  const label = (prepared.meta.name || prepared.script.name || prepared.script.id).replace(
    /[^\w.-]/g,
    "_",
  );

  const code =
    `(function(){\n` +
    `var __reg = window.__studioUserscriptRegistry;\n` +
    `var __b = __reg && __reg[${JSON.stringify(token)}];\n` +
    `if(!__b){ console.error(${JSON.stringify(`[userscript ${label}] bindings missing`)}); return; }\n` +
    `${preamble}\n` +
    `${requireCode}\n;\n` +
    `${prepared.script.source}\n` +
    `})();\n` +
    `//# sourceURL=studio-userscript://${label}.user.js\n`;

  return new Promise<void>((resolve) => {
    let url = "";
    const cleanup = () => {
      if (url) URL.revokeObjectURL(url);
      delete registry()[token];
      resolve();
    };
    try {
      const blob = new Blob([code], { type: "text/javascript" });
      url = URL.createObjectURL(blob);
      const el = document.createElement("script");
      el.src = url;
      el.addEventListener("load", cleanup, { once: true });
      el.addEventListener("error", cleanup, { once: true });
      (document.head || document.documentElement).appendChild(el);
    } catch (err) {
      console.error(`[userscript ${label}] injection failed`, err);
      cleanup();
    }
  });
}

/**
 * Run every enabled userscript whose @run-at matches `phase`. Safe to call
 * before the scripts are loaded — the first call kicks off preparation and all
 * three phase calls await the same prepared set. Never throws.
 */
export async function injectUserscriptsAt(phase: RunAt): Promise<void> {
  if (!preparedPromise) preparedPromise = prepareAll();
  let prepared: PreparedScript[];
  try {
    prepared = await preparedPromise;
  } catch (err) {
    console.error("[userscripts] preparation failed", err);
    return;
  }
  for (const p of prepared) {
    if (p.executed || p.meta.runAt !== phase) continue;
    p.executed = true;
    try {
      await executeScript(p);
    } catch (err) {
      console.error(`[userscript ${p.script.name}] execution failed`, err);
    }
  }
}
