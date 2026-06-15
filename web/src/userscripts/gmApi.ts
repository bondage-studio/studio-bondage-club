// Builds the per-script GM_* / GM.* API surface. The returned flat record is
// stashed in a per-injection registry; inject.ts destructures its keys as local
// variables inside the script's closure (so `GM_setValue(...)` etc. just work).

import type { Userscript } from "../types";
import { registerMenuCommand, unregisterMenuCommand } from "./menu";
import { toProxyURL, type UserscriptMetadata } from "./metadata";
import type { ValueStore } from "./values";

export interface ResourceData {
  text: string;
  /** Local proxy URL for the resource (usable as an <img>/<link> src). */
  url: string;
}

interface GmXhrDetails {
  method?: string;
  url: string;
  headers?: Record<string, string>;
  data?: XMLHttpRequestBodyInit | null;
  responseType?: XMLHttpRequestResponseType;
  timeout?: number;
  onload?: (response: GmXhrResponse) => void;
  onerror?: (response: GmXhrResponse) => void;
  ontimeout?: (response: GmXhrResponse) => void;
  onprogress?: (event: ProgressEvent) => void;
}

interface GmXhrResponse {
  readyState: number;
  status: number;
  statusText: string;
  responseHeaders: string;
  response: unknown;
  responseText: string;
  finalUrl: string;
}

function gmXmlHttpRequest(details: GmXhrDetails): { abort: () => void } {
  const xhr = new XMLHttpRequest();
  const method = (details.method || "GET").toUpperCase();
  // Route cross-origin requests through the local proxy.
  xhr.open(method, toProxyURL(details.url), true);
  if (details.responseType) xhr.responseType = details.responseType;
  if (details.timeout) xhr.timeout = details.timeout;
  if (details.headers) {
    for (const [k, v] of Object.entries(details.headers)) {
      // The browser forbids a few headers (User-Agent, Cookie, …); ignore those.
      try {
        xhr.setRequestHeader(k, v);
      } catch {
        // ignored
      }
    }
  }

  const build = (): GmXhrResponse => ({
    readyState: xhr.readyState,
    status: xhr.status,
    statusText: xhr.statusText,
    responseHeaders: xhr.getAllResponseHeaders(),
    response: xhr.response,
    responseText: xhr.responseType === "" || xhr.responseType === "text" ? xhr.responseText : "",
    finalUrl: details.url,
  });

  xhr.onload = () => details.onload?.(build());
  xhr.onerror = () => details.onerror?.(build());
  xhr.ontimeout = () => details.ontimeout?.(build());
  if (details.onprogress) xhr.onprogress = (e) => details.onprogress?.(e);

  xhr.send(details.data ?? null);
  return { abort: () => xhr.abort() };
}

function gmXmlHttpRequestPromise(details: GmXhrDetails): Promise<GmXhrResponse> {
  return new Promise((resolve, reject) => {
    gmXmlHttpRequest({
      ...details,
      onload: resolve,
      onerror: reject,
      ontimeout: reject,
    });
  });
}

function gmAddStyle(css: string): HTMLStyleElement {
  const style = document.createElement("style");
  style.textContent = css;
  (document.head || document.documentElement).appendChild(style);
  return style;
}

function gmSetClipboard(text: string): void {
  if (navigator.clipboard?.writeText) {
    void navigator.clipboard.writeText(text).catch(() => undefined);
    return;
  }
  const ta = document.createElement("textarea");
  ta.value = text;
  ta.style.position = "fixed";
  ta.style.opacity = "0";
  document.body.appendChild(ta);
  ta.select();
  try {
    document.execCommand("copy");
  } catch {
    // best-effort
  }
  ta.remove();
}

function gmOpenInTab(url: string, options?: { active?: boolean } | boolean) {
  const active = typeof options === "boolean" ? !options : (options?.active ?? true);
  const win = window.open(url, "_blank");
  if (win && !active) {
    try {
      win.blur();
      window.focus();
    } catch {
      // best-effort
    }
  }
  return {
    close: () => win?.close(),
    closed: () => win?.closed ?? true,
  };
}

interface GmNotificationDetails {
  text?: string;
  title?: string;
  onclick?: () => void;
}

function gmNotification(detailsOrText: GmNotificationDetails | string, title?: string): void {
  const details: GmNotificationDetails =
    typeof detailsOrText === "string" ? { text: detailsOrText, title } : detailsOrText;
  const show = () => {
    try {
      const n = new Notification(details.title ?? "Userscript", { body: details.text });
      if (details.onclick) n.onclick = details.onclick;
    } catch {
      console.log(`[notification] ${details.title ?? ""} ${details.text ?? ""}`);
    }
  };
  if (typeof Notification === "undefined") {
    console.log(`[notification] ${details.title ?? ""} ${details.text ?? ""}`);
    return;
  }
  if (Notification.permission === "granted") {
    show();
  } else if (Notification.permission !== "denied") {
    void Notification.requestPermission().then((p) => {
      if (p === "granted") show();
    });
  }
}

/**
 * Build the GM binding record for one script. Every key becomes a local variable
 * in the injected script's closure (see inject.ts), so the set here defines the
 * available API. unsafeWindow === window: scripts run in the page main world.
 */
export function buildGmBindings(
  script: Userscript,
  meta: UserscriptMetadata,
  values: ValueStore,
  resources: Record<string, ResourceData>,
): Record<string, unknown> {
  const gmInfo = {
    script: {
      name: meta.name || script.name,
      namespace: meta.namespace,
      version: meta.version || script.version || "",
      description: meta.description,
      author: meta.author,
      icon: meta.icon,
      grant: meta.grant,
      includes: meta.include,
      matches: meta.match,
      excludes: meta.exclude,
      resources: meta.resource,
      "run-at": meta.runAt,
    },
    scriptHandler: "Studio Bondage Club",
    version: "1.0",
    scriptMetaStr: meta.metaStr,
    scriptWillUpdate: script.autoUpdate,
  };

  const getValue = (key: string, def?: unknown) => values.get(key, def);
  const setValue = (key: string, value: unknown) => values.set(key, value);
  const deleteValue = (key: string) => values.delete(key);
  const listValues = () => values.list();

  const getResourceText = (name: string) => resources[name]?.text ?? "";
  const getResourceURL = (name: string) => resources[name]?.url ?? "";

  const registerMenu = (label: string, cb: (e?: Event) => void, accessKey?: string) =>
    registerMenuCommand(script.id, meta.name || script.name, label, cb, accessKey);

  // The modern Promise-based GM.* namespace.
  const gm = {
    info: gmInfo,
    getValue: (key: string, def?: unknown) => Promise.resolve(values.get(key, def)),
    setValue: (key: string, value: unknown) => values.setAsync(key, value),
    deleteValue: (key: string) => values.deleteAsync(key),
    listValues: () => Promise.resolve(values.list()),
    getResourceText: (name: string) => Promise.resolve(getResourceText(name)),
    getResourceUrl: (name: string) => Promise.resolve(getResourceURL(name)),
    xmlHttpRequest: gmXmlHttpRequestPromise,
    registerMenuCommand: registerMenu,
    unregisterMenuCommand: unregisterMenuCommand,
    addStyle: (css: string) => Promise.resolve(gmAddStyle(css)),
    setClipboard: gmSetClipboard,
    openInTab: gmOpenInTab,
    notification: gmNotification,
    log: (...args: unknown[]) => console.log(`[${meta.name || script.name}]`, ...args),
  };

  return {
    GM_info: gmInfo,
    GM_getValue: getValue,
    GM_setValue: setValue,
    GM_deleteValue: deleteValue,
    GM_listValues: listValues,
    GM_getResourceText: getResourceText,
    GM_getResourceURL: getResourceURL,
    GM_xmlhttpRequest: gmXmlHttpRequest,
    GM_registerMenuCommand: registerMenu,
    GM_unregisterMenuCommand: unregisterMenuCommand,
    GM_addStyle: gmAddStyle,
    GM_setClipboard: gmSetClipboard,
    GM_openInTab: gmOpenInTab,
    GM_notification: gmNotification,
    GM_log: (...args: unknown[]) => console.log(`[${meta.name || script.name}]`, ...args),
    GM: gm,
    unsafeWindow: window,
  };
}
