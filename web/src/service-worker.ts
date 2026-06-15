/// <reference lib="webworker" />

declare const self: ServiceWorkerGlobalScope;

const LOCAL_ORIGIN = self.location.origin;

self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

// Whether this engine supports streaming request bodies (a ReadableStream body
// with duplex: "half"). Chromium does; Firefox/GeckoView do not — there,
// forwarding request.body truncates the upload and breaks multipart boundary
// parsing upstream ("No initial boundary string"). Detected once via the
// standard duplex probe: a supporting engine reads the duplex getter and omits
// the auto Content-Type for a stream body.
const supportsRequestStreams: boolean = (() => {
  let duplexAccessed = false;
  try {
    const hasContentType = new Request("https://example.com", {
      method: "POST",
      body: new ReadableStream(),
      get duplex() {
        duplexAccessed = true;
        return "half";
      },
    } as RequestInit).headers.has("Content-Type");
    return duplexAccessed && !hasContentType;
  } catch {
    return false;
  }
})();

self.addEventListener("fetch", (event) => {
  const { request } = event;
  const loaderURL = toRemoteLoaderURL(request.url);
  if (!loaderURL) {
    return;
  }
  event.respondWith(forwardToLoader(request, loaderURL));
});

async function forwardToLoader(request: Request, loaderURL: string): Promise<Response> {
  const hasBody = request.method !== "GET" && request.method !== "HEAD";
  const init: RequestInit = {
    method: request.method,
    cache: request.cache === "only-if-cached" ? "default" : request.cache,
    credentials: "same-origin",
    headers: new Headers(request.headers),
    redirect: "follow",
  };
  if (hasBody) {
    if (supportsRequestStreams && request.body) {
      // Stream the body through (no extra buffering) where supported.
      init.body = request.body;
      (init as RequestInit & { duplex: "half" }).duplex = "half";
    } else {
      // Fallback: buffer the body so engines without streaming-upload support
      // send the complete payload. The original Content-Type (incl. the
      // multipart boundary) is preserved via the copied headers, and the
      // browser recomputes Content-Length from the buffered body.
      init.body = await request.arrayBuffer();
    }
  }
  return fetch(loaderURL, init);
}

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
