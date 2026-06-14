/// <reference lib="webworker" />

declare const self: ServiceWorkerGlobalScope;

const LOCAL_ORIGIN = self.location.origin;

self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("fetch", (event) => {
  const { request } = event;
  const loaderURL = toRemoteLoaderURL(request.url);
  if (!loaderURL) {
    return;
  }
  const hasBody = request.method !== "GET" && request.method !== "HEAD";
  event.respondWith(
    fetch(loaderURL, {
      method: request.method,
      cache: request.cache === "only-if-cached" ? "default" : request.cache,
      credentials: "same-origin",
      headers: new Headers(request.headers),
      body: hasBody ? request.body : undefined,
      redirect: "follow",
      // duplex is required by the spec when body is a ReadableStream
      ...(hasBody && request.body ? { duplex: "half" } : {}),
    } as RequestInit)
  );
});

// Returns a local proxy URL for cross-origin requests, or null to pass through.
//
// Same-origin requests are passed directly to the local backend without SW
// interception. This includes both game assets (e.g.
// http://127.0.0.1:8080/Screens/...) and the game socket: the client hooks
// CommonGetServer() to return the local origin, so socket.io connects to the
// same-origin /socket.io/ endpoint, which the local backend forwards to the
// configured game server (WebSocket upgrades bypass the SW entirely).
//
// Cross-origin mod/CDN resources (e.g. cdn.jsdelivr.net) are routed through the
// local server at /{original-url}, where the local backend caches them.
function toRemoteLoaderURL(rawURL: string): string | null {
  let url: URL;
  try {
    url = new URL(rawURL);
  } catch {
    return null;
  }
  if (url.origin === LOCAL_ORIGIN) {
    return null;
  }
  return new URL("/" + url.href, LOCAL_ORIGIN).href;
}

export {};
