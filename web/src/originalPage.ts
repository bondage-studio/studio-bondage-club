import { isAndroidRuntime } from "@/lib/platform";
import { installOptimizationHost } from "@/optimizations/host";
import { rpcClient } from "@/rpc/client";
import { setRpcToken } from "@/rpc/token";
import { injectUserscriptsAt } from "@/userscripts/inject";

interface StudioBootstrap {
  upstreamBase: string;
  serviceWorkerPath: string;
  adminRootID: string;
  statusRootID: string;
  defaultLocalGameServer: boolean;
}

export type GameServerMode = "local" | "remote";

interface HomepageSourceResponse {
  html: string;
  url: string;
  statusCode: number;
  cacheStatus: string;
}

const bootstrapScriptID = "studio-bootstrap-data";
const urlAttributes = ["href", "src", "poster", "data", "action"];

interface CapturedLoadListener {
  listener: EventListenerOrEventListenerObject;
  options: boolean | AddEventListenerOptions | undefined;
  registeredAfterLoad: boolean;
  removed: boolean;
}

interface CapturedOnLoadHandler {
  handler: ((this: GlobalEventHandlers, ev: Event) => unknown) | null;
  registeredAfterLoad: boolean;
}

export function readStudioBootstrap(): StudioBootstrap | null {
  const script = document.getElementById(bootstrapScriptID);
  if (!script?.textContent) {
    setStatus(null, "error", "Studio bootstrap data is missing.");
    return null;
  }
  try {
    const data = JSON.parse(script.textContent) as StudioBootstrap & { rpcToken?: string };
    // Capture the RPC capability token into a private closure and erase every
    // trace of it before any injected userscript runs: drop it from the parsed
    // object and remove the bootstrap <script> from the DOM. The token is never
    // placed on window.
    setRpcToken(data.rpcToken ?? "");
    delete data.rpcToken;
    script.remove();
    return data;
  } catch (error) {
    console.error("Failed to parse Studio bootstrap data", error);
    setStatus(null, "error", "Studio bootstrap data could not be parsed.");
    return null;
  }
}

export async function restoreOriginalHomepage(bootstrap: StudioBootstrap | null) {
  if (!bootstrap) {
    return;
  }

  try {
    setStatus(bootstrap, "loading", "Registering service worker.");
    const useServiceWorkerRoutes = await registerServiceWorker(bootstrap);
    installMediaProxy();

    setStatus(bootstrap, "loading", "Fetching original homepage from local cache.");
    const source = await fetchHomepageSource();

    setStatus(
      bootstrap,
      "loading",
      `Restoring original homepage (${source.cacheStatus || "cache"}).`,
    );
    await restoreParsedHomepage(bootstrap, source, useServiceWorkerRoutes);
  } catch (error) {
    console.error("Failed to restore original homepage", error);
    setStatus(bootstrap, "error", errorMessage(error));
  }
}

async function fetchHomepageSource(): Promise<HomepageSourceResponse> {
  const source = await rpcClient.call<HomepageSourceResponse>("homepage.get", {});
  if (!source.html) {
    throw new Error("Original homepage endpoint returned an empty HTML document.");
  }
  return source;
}

async function registerServiceWorker(bootstrap: StudioBootstrap) {
  // The Android WebView host (MainActivity) intercepts cross-origin requests
  // natively via shouldInterceptRequest and tags its user agent, so the service
  // worker is redundant there. Returning false routes through the same
  // parse-time URL-rewriting fallback used when service workers are unavailable.
  if (isAndroidRuntime()) {
    setStatus(bootstrap, "loading", "Native WebView interception active; service worker disabled.");
    return false;
  }
  if (!("serviceWorker" in navigator)) {
    setStatus(
      bootstrap,
      "loading",
      "Service workers are unavailable; continuing with local URL rewriting.",
    );
    return false;
  }
  try {
    const serviceWorkerURL = new URL(bootstrap.serviceWorkerPath, window.location.origin);
    serviceWorkerURL.searchParams.set("v", "remote-loader-v2");
    const registration = await navigator.serviceWorker.register(serviceWorkerURL, {
      scope: "/",
      type: "module",
    });
    await registration.update().catch(() => undefined);
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller || registration.installing || registration.waiting) {
      await waitForControllerChange();
    }
    if (navigator.serviceWorker.controller) {
      return true;
    }
    setStatus(
      bootstrap,
      "loading",
      "Service worker is not controlling this page yet; continuing with local URL rewriting.",
    );
    return false;
  } catch (error) {
    console.warn("Studio service worker registration failed", error);
    setStatus(
      bootstrap,
      "loading",
      "Service worker registration failed; continuing with local URL rewriting.",
    );
    return false;
  }
}

// Redirects the Bondage Club game socket to the local studio server.
//
// BC's CommonGetServer() (Common.js) returns the public game server URL — and,
// when not on a bondageprojects domain, the *test* server. We override it to
// return our local origin so socket.io connects same-origin, which the backend
// transparently forwards (both HTTP long-polling and the WebSocket upgrade).
//
// Timing: GameStart() (Game.js) calls CommonGetServer() at runtime on window
// load, after all game scripts have executed. This hook is installed after the
// scripts run but before the load replay fires (see restoreParsedHomepage), so
// the override is in place when GameStart() reads it. Overriding the global
// works because CommonGetServer is a function declaration on window.
function installCommonServerHook() {
  (window as unknown as { CommonGetServer?: () => string }).CommonGetServer = () =>
    window.location.origin;
}

// CommonGetServer() always returns our origin, so the local/remote choice can't
// ride on its return value (socket.io would read a path there as a *namespace*,
// not a distinct HTTP endpoint). Instead the backend exposes two stable paths —
// /proxy/socket.io (reverse proxy) and /local/socket.io (built-in) — and we wrap
// socket.io's client to inject the matching `path` option. The frontend owns the
// switch: flipping it forces a reconnect onto the other endpoint, which is the
// only clean way to move a stateful engine.io session across backends.

const GAME_SERVER_STORAGE_KEY = "studio.gameServer";

let gameServerMode: GameServerMode = "remote";
let gameServerModeResolved = false;

const gameServerSocketPath: Record<GameServerMode, string> = {
  local: "/local/socket.io",
  remote: "/proxy/socket.io",
};

// Resolve the initial mode lazily and once: a localStorage override wins, else the
// server-provided default from the bootstrap. Lazy because the panel may read the
// mode before the async homepage restore runs, and the io wrapper reads it at
// connect time — both must see the same resolved value regardless of order.
function ensureGameServerMode() {
  if (gameServerModeResolved) return;
  gameServerModeResolved = true;
  let stored: string | null = null;
  try {
    stored = localStorage.getItem(GAME_SERVER_STORAGE_KEY);
  } catch {
    // Ignore; fall back to the server-provided default.
  }
  if (stored === "local" || stored === "remote") {
    gameServerMode = stored;
    return;
  }
  gameServerMode = readStudioBootstrap()?.defaultLocalGameServer ? "local" : "remote";
}

export function getGameServerMode(): GameServerMode {
  ensureGameServerMode();
  return gameServerMode;
}

// setGameServerMode persists the choice and forces the live BC connection to
// reconnect onto the newly selected endpoint. Safe to call from the panel.
export function setGameServerMode(mode: GameServerMode) {
  gameServerMode = mode;
  gameServerModeResolved = true;
  try {
    localStorage.setItem(GAME_SERVER_STORAGE_KEY, mode);
  } catch {
    // Private-mode / disabled storage: the choice still applies for this session.
  }
  reconnectGameServer();
}

// Wrap window.io so every socket.io connection carries the path for the current
// mode. Installed after the game scripts define `io` but before GameStart() calls
// it. The wrapper reads gameServerMode lazily, so a later switch + reconnect
// picks up the new endpoint without re-wrapping.
function installIoPathHook() {
  const w = window as unknown as {
    io?: ((...args: unknown[]) => unknown) & Record<string, unknown>;
  };
  const realIo = w.io;
  if (typeof realIo !== "function") {
    return;
  }
  const wrapped = ((url?: unknown, opts?: Record<string, unknown>) =>
    realIo(url, { ...(opts ?? {}), path: gameServerSocketPath[gameServerMode] })) as ((
    ...args: unknown[]
  ) => unknown) &
    Record<string, unknown>;
  // Preserve io.protocol, io.Manager, io.Socket, io.connect, etc.
  Object.assign(wrapped, realIo);
  wrapped.connect = wrapped;
  w.io = wrapped;
}

function reconnectGameServer() {
  const w = window as unknown as {
    ServerSocket?: { disconnect?: () => void };
    ServerInit?: () => void;
  };
  try {
    w.ServerSocket?.disconnect?.();
  } catch (error) {
    console.warn("Studio: failed to disconnect current game socket", error);
  }
  if (typeof w.ServerInit === "function") {
    try {
      w.ServerInit();
    } catch (error) {
      console.error("Studio: failed to reconnect game socket", error);
    }
  }
}

function installMediaProxy() {
  // Patch Audio constructor: new Audio(src)
  const NativeAudio = window.Audio;
  function StudioAudio(src?: string): HTMLAudioElement {
    return new NativeAudio(src ? toLocalProxyURL(src) : src);
  }
  StudioAudio.prototype = NativeAudio.prototype;
  window.Audio = StudioAudio as unknown as typeof Audio;

  // Patch HTMLMediaElement.prototype.src setter — covers both <audio> and <video>
  // for element.src = url and document.createElement("audio/video") + src assignment.
  const nativeSrcDescriptor =
    Object.getOwnPropertyDescriptor(HTMLAudioElement.prototype, "src") ??
    Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, "src");
  if (nativeSrcDescriptor?.set) {
    const nativeSet = nativeSrcDescriptor.set;
    Object.defineProperty(HTMLMediaElement.prototype, "src", {
      ...nativeSrcDescriptor,
      set(value: string) {
        nativeSet.call(this, toLocalProxyURL(value));
      },
    });
  }
}

function toLocalProxyURL(src: string): string {
  let url: URL;
  try {
    url = new URL(src, window.location.href);
  } catch {
    return src;
  }
  if (url.origin === window.location.origin) {
    return src;
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") {
    return src;
  }
  return new URL("/" + url.href, window.location.origin).href;
}

function waitForControllerChange() {
  return new Promise<void>((resolve) => {
    const timeout = window.setTimeout(resolve, 1200);
    navigator.serviceWorker.addEventListener(
      "controllerchange",
      () => {
        window.clearTimeout(timeout);
        resolve();
      },
      { once: true },
    );
  });
}

async function restoreParsedHomepage(
  bootstrap: StudioBootstrap,
  source: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  const parsed = new DOMParser().parseFromString(source.html, "text/html");
  document.getElementById(bootstrap.statusRootID)?.remove();

  copyAttributes(document.documentElement, parsed.documentElement);
  copyAttributes(document.body, parsed.body);

  const loadReplay = installWindowLoadReplay();
  try {
    // Advertise the BMM host bridge before any userscript (incl. the BMM loader)
    // runs, so window.__bmmHost is in place when the BMM bundle captures it.
    installOptimizationHost();

    // Userscripts @run-at document-start: before the page's own scripts run.
    await injectUserscriptsAt("document-start");

    // Insert all elements without blocking on individual script loads.
    // Scripts with async=false (set in prepareScript) execute in insertion
    // order, so the browser can download them in parallel while still
    // preserving correct execution order.
    const scriptPromises: Promise<void>[] = [];
    appendDocumentHead(parsed, bootstrap, source, useServiceWorkerRoutes, scriptPromises);
    appendDocumentBody(parsed, bootstrap, source, useServiceWorkerRoutes, scriptPromises);
    await Promise.allSettled(scriptPromises);

    // Userscripts @run-at document-end: after page scripts loaded, before the
    // load replay fires GameStart().
    await injectUserscriptsAt("document-end");

    // Override CommonGetServer and wrap socket.io now that game scripts have
    // defined them, but before the load replay triggers GameStart() (which reads
    // them). See the hook comments.
    ensureGameServerMode();
    installCommonServerHook();
    installIoPathHook();
    loadReplay.replayMissedLoad();

    // Userscripts @run-at document-idle (the default): after the load replay.
    await injectUserscriptsAt("document-idle");
  } finally {
    loadReplay.uninstall();
  }
}

function installWindowLoadReplay() {
  const nativeAddEventListener = window.addEventListener;
  const nativeRemoveEventListener = window.removeEventListener;
  const capturedListeners: CapturedLoadListener[] = [];
  let loadHasFired = document.readyState === "complete";

  const markLoadFired = () => {
    loadHasFired = true;
  };
  nativeAddEventListener.call(window, "load", markLoadFired, { once: true });

  const onLoadTracker = installWindowOnLoadTracker(() => loadHasFired);

  const patchedAddEventListener = function (
    this: Window,
    type: string,
    listener: EventListenerOrEventListenerObject | null,
    options?: boolean | AddEventListenerOptions,
  ) {
    if (this === window && type === "load" && listener) {
      captureLoadListener(capturedListeners, listener, options, loadHasFired);
    }
    nativeAddEventListener.call(
      this,
      type,
      listener as EventListenerOrEventListenerObject,
      options,
    );
  };

  const patchedRemoveEventListener = function (
    this: Window,
    type: string,
    listener: EventListenerOrEventListenerObject | null,
    options?: boolean | EventListenerOptions,
  ) {
    if (this === window && type === "load" && listener) {
      markCapturedLoadListenerRemoved(capturedListeners, listener, options);
    }
    nativeRemoveEventListener.call(
      this,
      type,
      listener as EventListenerOrEventListenerObject,
      options,
    );
  };

  window.addEventListener = patchedAddEventListener as typeof window.addEventListener;
  window.removeEventListener = patchedRemoveEventListener as typeof window.removeEventListener;

  return {
    replayMissedLoad() {
      if (!loadHasFired) {
        return;
      }

      const event = createReplayedLoadEvent();
      for (const entry of capturedListeners) {
        if (!shouldReplayLoadListener(entry)) {
          continue;
        }
        invokeWindowLoadListener(entry.listener, event);
        if (eventListenerOnce(entry.options)) {
          entry.removed = true;
          nativeRemoveEventListener.call(window, "load", entry.listener, entry.options);
        }
      }

      const onLoadHandler = onLoadTracker.missedHandler();
      if (onLoadHandler) {
        invokeWindowOnLoadHandler(onLoadHandler, event);
      }
    },
    uninstall() {
      window.addEventListener = nativeAddEventListener;
      window.removeEventListener = nativeRemoveEventListener;
      nativeRemoveEventListener.call(window, "load", markLoadFired);
      onLoadTracker.uninstall();
    },
  };
}

function captureLoadListener(
  capturedListeners: CapturedLoadListener[],
  listener: EventListenerOrEventListenerObject,
  options: boolean | AddEventListenerOptions | undefined,
  registeredAfterLoad: boolean,
) {
  const capture = eventListenerCapture(options);
  const existing = capturedListeners.find(
    (entry) =>
      !entry.removed &&
      entry.listener === listener &&
      eventListenerCapture(entry.options) === capture,
  );
  if (existing) {
    return;
  }

  capturedListeners.push({
    listener,
    options,
    registeredAfterLoad,
    removed: false,
  });
}

function markCapturedLoadListenerRemoved(
  capturedListeners: CapturedLoadListener[],
  listener: EventListenerOrEventListenerObject,
  options: boolean | EventListenerOptions | undefined,
) {
  const capture = eventListenerCapture(options);
  const entry = capturedListeners.find(
    (candidate) =>
      !candidate.removed &&
      candidate.listener === listener &&
      eventListenerCapture(candidate.options) === capture,
  );
  if (entry) {
    entry.removed = true;
  }
}

function shouldReplayLoadListener(entry: CapturedLoadListener) {
  return entry.registeredAfterLoad && !entry.removed && !eventListenerSignalAborted(entry.options);
}

function installWindowOnLoadTracker(loadHasFired: () => boolean) {
  const ownDescriptor = Object.getOwnPropertyDescriptor(window, "onload");
  const descriptor = findPropertyDescriptor(window, "onload");
  let currentHandler = window.onload;
  let assignment: CapturedOnLoadHandler | null = null;

  const getNativeOnLoad = () => {
    if (descriptor?.get) {
      return descriptor.get.call(window) as typeof window.onload;
    }
    return currentHandler;
  };

  const setNativeOnLoad = (value: typeof window.onload) => {
    currentHandler = value;
    if (descriptor?.set) {
      descriptor.set.call(window, value);
    }
  };

  try {
    Object.defineProperty(window, "onload", {
      configurable: true,
      enumerable: descriptor?.enumerable ?? true,
      get() {
        return getNativeOnLoad();
      },
      set(value: typeof window.onload) {
        setNativeOnLoad(value);
        assignment = {
          handler: value,
          registeredAfterLoad: loadHasFired(),
        };
      },
    });
  } catch (error) {
    console.warn("Studio load replay could not track window.onload", error);
    return {
      missedHandler: () => null,
      uninstall: () => undefined,
    };
  }

  return {
    missedHandler() {
      const handler = assignment?.handler;
      if (!assignment?.registeredAfterLoad || !handler || handler !== getNativeOnLoad()) {
        return null;
      }
      return handler;
    },
    uninstall() {
      const finalHandler = getNativeOnLoad();
      if (ownDescriptor) {
        Object.defineProperty(window, "onload", ownDescriptor);
      } else {
        delete (window as { onload?: typeof window.onload }).onload;
      }
      window.onload = finalHandler;
    },
  };
}

function findPropertyDescriptor(
  target: object,
  property: PropertyKey,
): PropertyDescriptor | undefined {
  let current: object | null = target;
  while (current) {
    const descriptor = Object.getOwnPropertyDescriptor(current, property);
    if (descriptor) {
      return descriptor;
    }
    current = Object.getPrototypeOf(current);
  }
  return undefined;
}

function eventListenerCapture(options: boolean | EventListenerOptions | undefined) {
  return typeof options === "boolean" ? options : Boolean(options?.capture);
}

function eventListenerOnce(options: boolean | AddEventListenerOptions | undefined) {
  return typeof options === "object" && Boolean(options.once);
}

function eventListenerSignalAborted(options: boolean | AddEventListenerOptions | undefined) {
  return typeof options === "object" && Boolean(options.signal?.aborted);
}

function createReplayedLoadEvent() {
  const event = new Event("load");
  defineEventValue(event, "target", window);
  defineEventValue(event, "currentTarget", window);
  defineEventValue(event, "srcElement", window);
  return event;
}

function defineEventValue(event: Event, property: keyof Event, value: unknown) {
  try {
    Object.defineProperty(event, property, {
      configurable: true,
      value,
    });
  } catch {
    // Browser Event properties may be non-configurable; listeners still get the correct this value.
  }
}

function invokeWindowLoadListener(listener: EventListenerOrEventListenerObject, event: Event) {
  try {
    if (typeof listener === "function") {
      listener.call(window, event);
    } else {
      listener.handleEvent(event);
    }
  } catch (error) {
    reportReplayError(error);
  }
}

function invokeWindowOnLoadHandler(
  handler: (this: GlobalEventHandlers, ev: Event) => unknown,
  event: Event,
) {
  try {
    handler.call(window, event);
  } catch (error) {
    reportReplayError(error);
  }
}

function reportReplayError(error: unknown) {
  if ("reportError" in window) {
    window.reportError(error);
  } else {
    globalThis.setTimeout(() => {
      throw error;
    }, 0);
  }
}

function appendDocumentHead(
  parsed: Document,
  bootstrap: StudioBootstrap,
  source: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
  scriptPromises: Promise<void>[],
) {
  for (const child of Array.from(parsed.head.childNodes)) {
    const node = prepareNode(child, bootstrap, source, useServiceWorkerRoutes);
    if (node instanceof HTMLScriptElement && shouldDeferScript(node)) {
      // Deferred/module scripts execute after the parsed body is inserted.
      scriptPromises.push(
        appendPreparedNode(document.body, node, document.getElementById(bootstrap.adminRootID)),
      );
      continue;
    }
    if (node instanceof HTMLScriptElement) {
      // async=false scripts execute in insertion order without blocking parsing.
      scriptPromises.push(appendPreparedNode(document.head, node, null));
    } else {
      document.head.appendChild(node);
    }
  }
}

function appendDocumentBody(
  parsed: Document,
  bootstrap: StudioBootstrap,
  source: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
  scriptPromises: Promise<void>[],
) {
  const adminRoot = document.getElementById(bootstrap.adminRootID);
  for (const child of Array.from(parsed.body.childNodes)) {
    const node = prepareNode(child, bootstrap, source, useServiceWorkerRoutes);
    if (node instanceof HTMLScriptElement) {
      scriptPromises.push(appendPreparedNode(document.body, node, adminRoot));
    } else {
      document.body.insertBefore(node, adminRoot);
    }
  }
}

function prepareNode(
  source: Node,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
): Node {
  if (source instanceof HTMLScriptElement) {
    return prepareScript(source, bootstrap, homepage, useServiceWorkerRoutes);
  }
  const clone = source.cloneNode(true);
  rewriteResourceURLs(clone, bootstrap, homepage, useServiceWorkerRoutes);
  return clone;
}

function prepareScript(
  source: HTMLScriptElement,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  const script = document.createElement("script");
  for (const attr of Array.from(source.attributes)) {
    script.setAttribute(attr.name, attr.value);
  }
  const src = source.getAttribute("src");
  if (src) {
    script.src = localResourceURL(src, bootstrap, homepage, useServiceWorkerRoutes);
  }
  script.text = source.text;
  if (!source.hasAttribute("async")) {
    script.async = false;
  }
  return script;
}

function shouldDeferScript(script: HTMLScriptElement) {
  const type = script.getAttribute("type")?.trim().toLowerCase();
  return script.defer || type === "module";
}

function appendPreparedNode(parent: Node, node: Node, before: Node | null): Promise<void> {
  if (!(node instanceof HTMLScriptElement)) {
    parent.insertBefore(node, before);
    return Promise.resolve();
  }

  return new Promise((resolve) => {
    const done = () => resolve();
    node.addEventListener("load", done, { once: true });
    node.addEventListener("error", done, { once: true });
    parent.insertBefore(node, before);
    if (!node.src) {
      window.setTimeout(resolve, 0);
    }
  });
}

function copyAttributes(target: Element, source: Element | null) {
  if (!source) {
    return;
  }
  for (const attr of Array.from(target.attributes)) {
    if (attr.name.startsWith("data-studio")) {
      continue;
    }
    target.removeAttribute(attr.name);
  }
  for (const attr of Array.from(source.attributes)) {
    target.setAttribute(attr.name, attr.value);
  }
}

function rewriteResourceURLs(
  root: Node,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  if (root instanceof Element) {
    rewriteElementURLs(root, bootstrap, homepage, useServiceWorkerRoutes);
  }
  if (!(root instanceof Element) && !(root instanceof DocumentFragment)) {
    return;
  }
  const elements = root.querySelectorAll("*");
  for (const element of Array.from(elements)) {
    rewriteElementURLs(element, bootstrap, homepage, useServiceWorkerRoutes);
  }
}

function rewriteElementURLs(
  element: Element,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  for (const attr of urlAttributes) {
    const value = element.getAttribute(attr);
    if (value) {
      element.setAttribute(
        attr,
        localResourceURL(value, bootstrap, homepage, useServiceWorkerRoutes),
      );
    }
  }

  const srcset = element.getAttribute("srcset");
  if (srcset) {
    element.setAttribute(
      "srcset",
      rewriteSrcset(srcset, bootstrap, homepage, useServiceWorkerRoutes),
    );
  }

  const style = element.getAttribute("style");
  if (style) {
    element.setAttribute(
      "style",
      rewriteCSSURLs(style, bootstrap, homepage, useServiceWorkerRoutes),
    );
  }

  if (element instanceof HTMLStyleElement && element.textContent) {
    element.textContent = rewriteCSSURLs(
      element.textContent,
      bootstrap,
      homepage,
      useServiceWorkerRoutes,
    );
  }
}

function rewriteSrcset(
  value: string,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  return value
    .split(",")
    .map((candidate) => {
      const trimmed = candidate.trim();
      if (!trimmed || trimmed.startsWith("data:")) {
        return trimmed;
      }
      const [url, ...descriptors] = trimmed.split(/\s+/);
      return [
        localResourceURL(url, bootstrap, homepage, useServiceWorkerRoutes),
        ...descriptors,
      ].join(" ");
    })
    .join(", ");
}

function rewriteCSSURLs(
  value: string,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  return value.replace(/url\((["']?)([^"')]+)\1\)/g, (_match, quote: string, rawURL: string) => {
    return `url(${quote}${localResourceURL(rawURL.trim(), bootstrap, homepage, useServiceWorkerRoutes)}${quote})`;
  });
}

function localResourceURL(
  rawValue: string,
  bootstrap: StudioBootstrap,
  homepage: HomepageSourceResponse,
  useServiceWorkerRoutes: boolean,
) {
  const value = rawValue.trim();
  if (
    !value ||
    value.startsWith("#") ||
    /^(?:about|blob|data|javascript|mailto|tel):/i.test(value)
  ) {
    return rawValue;
  }

  let resolved: URL;
  try {
    resolved = new URL(value, homepage.url);
  } catch {
    return rawValue;
  }

  if (isUpstreamResource(resolved, bootstrap)) {
    return localProxyResourceURL(resolved, bootstrap) ?? resolved.href;
  }

  return resolved.href;
}

function isUpstreamResource(resolved: URL, bootstrap: StudioBootstrap) {
  let upstream: URL;
  try {
    upstream = new URL(bootstrap.upstreamBase);
  } catch {
    return false;
  }

  const upstreamPath = upstream.pathname.endsWith("/")
    ? upstream.pathname
    : `${upstream.pathname}/`;
  return resolved.origin === upstream.origin && resolved.pathname.startsWith(upstreamPath);
}

function localProxyResourceURL(resolved: URL, bootstrap: StudioBootstrap) {
  let upstream: URL;
  try {
    upstream = new URL(bootstrap.upstreamBase);
  } catch {
    return null;
  }

  const upstreamPath = upstream.pathname.endsWith("/")
    ? upstream.pathname
    : `${upstream.pathname}/`;
  if (resolved.origin !== upstream.origin || !resolved.pathname.startsWith(upstreamPath)) {
    return null;
  }

  const relativePath = resolved.pathname.slice(upstreamPath.length);
  if (!relativePath) {
    return "/";
  }

  const local = new URL(`/${relativePath}`, window.location.origin);
  local.search = resolved.search;
  local.hash = resolved.hash;
  return `${local.pathname}${local.search}${local.hash}`;
}

function setStatus(bootstrap: StudioBootstrap | null, state: "loading" | "error", message: string) {
  const root = document.getElementById(bootstrap?.statusRootID ?? "studio-homepage-status");
  if (!root) {
    return;
  }
  root.setAttribute("data-state", state);
  const text = root.querySelector("[data-studio-status-text]");
  if (text) {
    text.textContent = message;
  }
}

function errorMessage(error: unknown) {
  return error instanceof Error
    ? error.message
    : "Unexpected error while loading original homepage.";
}
