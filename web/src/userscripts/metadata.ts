// Parser for the `// ==UserScript== ... // ==/UserScript==` metadata block of a
// `.user.js` file. The C++ background checker parses only @version/@downloadURL/
// @updateURL; this is the fuller client-side parse used for injection + display.

export type RunAt = "document-start" | "document-end" | "document-idle";

export interface ResourceEntry {
  name: string;
  url: string;
}

export interface UserscriptMetadata {
  name: string;
  namespace: string;
  version: string;
  description: string;
  author: string;
  icon: string;
  downloadURL: string;
  updateURL: string;
  grant: string[];
  require: string[];
  resource: ResourceEntry[];
  /** @match/@include/@exclude are parsed for GM_info display only — not used to
   *  gate execution (there is exactly one game page). */
  match: string[];
  include: string[];
  exclude: string[];
  runAt: RunAt;
  /** Raw metadata block text, for GM_info.scriptMetaStr. */
  metaStr: string;
}

const BLOCK_RE = /\/\/\s*==UserScript==([\s\S]*?)\/\/\s*==\/UserScript==/;

/** Map a raw @run-at value to one of our three injection phases. */
function normalizeRunAt(raw: string): RunAt {
  switch (raw) {
    case "document-start":
      return "document-start";
    case "document-body":
    case "document-end":
      return "document-end";
    case "document-idle":
    case "":
      // Tampermonkey's default is document-idle.
      return "document-idle";
    default:
      return "document-idle";
  }
}

export function parseMetadata(source: string): UserscriptMetadata {
  const meta: UserscriptMetadata = {
    name: "",
    namespace: "",
    version: "",
    description: "",
    author: "",
    icon: "",
    downloadURL: "",
    updateURL: "",
    grant: [],
    require: [],
    resource: [],
    match: [],
    include: [],
    exclude: [],
    runAt: "document-idle",
    metaStr: ""
  };

  const block = BLOCK_RE.exec(source);
  if (!block) return meta;
  meta.metaStr = block[1];

  let runAtRaw = "";
  for (const line of block[1].split(/\r?\n/)) {
    const m = /^\s*\/\/\s*@([\w-]+)\s*(.*)$/.exec(line);
    if (!m) continue;
    const key = m[1];
    const value = m[2].trim();
    switch (key) {
      case "name":
        if (!meta.name) meta.name = value;
        break;
      case "namespace":
        meta.namespace = value;
        break;
      case "version":
        meta.version = value;
        break;
      case "description":
        if (!meta.description) meta.description = value;
        break;
      case "author":
        meta.author = value;
        break;
      case "icon":
        meta.icon = value;
        break;
      case "downloadURL":
        meta.downloadURL = value;
        break;
      case "updateURL":
        meta.updateURL = value;
        break;
      case "grant":
        if (value) meta.grant.push(value);
        break;
      case "require":
        if (value) meta.require.push(value);
        break;
      case "resource": {
        // "@resource <name> <url>"
        const parts = value.split(/\s+/);
        if (parts.length >= 2) meta.resource.push({ name: parts[0], url: parts.slice(1).join(" ") });
        break;
      }
      case "match":
        if (value) meta.match.push(value);
        break;
      case "include":
        if (value) meta.include.push(value);
        break;
      case "exclude":
        if (value) meta.exclude.push(value);
        break;
      case "run-at":
        runAtRaw = value;
        break;
      default:
        break;
    }
  }
  meta.runAt = normalizeRunAt(runAtRaw);
  return meta;
}

/**
 * Convert an absolute http(s) URL into the local `/<absolute-url>` proxy path so
 * cross-origin fetches (GM_xmlhttpRequest, @require, @resource, install-from-URL)
 * route through the existing remote loader. Same-origin and non-http(s) URLs are
 * returned unchanged. Mirrors originalPage.ts `toLocalProxyURL`.
 */
export function toProxyURL(rawUrl: string): string {
  let url: URL;
  try {
    url = new URL(rawUrl, window.location.href);
  } catch {
    return rawUrl;
  }
  if (url.origin === window.location.origin) return rawUrl;
  if (url.protocol !== "http:" && url.protocol !== "https:") return rawUrl;
  return new URL("/" + url.href, window.location.origin).href;
}
