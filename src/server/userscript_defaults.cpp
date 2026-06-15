#include "server/userscript_defaults.hpp"

namespace sbc::server {

namespace {

// Bootstrap sources embedded verbatim from the upstream loaders so the defaults
// work immediately on a fresh install (offline-safe). They stay current via the
// background updater + the user's confirmation, like any other script.
constexpr const char* kBmmSource = R"SBCJS(// ==UserScript==
// @name         BC Mod Manager
// @version      1.1
// @include      /^https:\/\/(www\.)?bondage(projects\.elementfx|-(europe|asia))\.com\/.*/
// @grant        none
// @run-at       document-start
// ==/UserScript==

(function () {
  "use strict";
  if (window.__bmmLoaderBootstrapped) return;
  window.__bmmLoaderBootstrapped = true;

  const LOADER_URL = "https://bondage-studio.github.io/bc-mod-manager/main.js";
  const VERSION_KEY = "bmm:loaderVersion";

  // Shared state so the in-page loader (main.js) can tell whether the cached build
  // it is running is stale. `loadedVersion` is the pinned hash we booted from (null
  // on a fresh / fallback load whose content is current by definition); `latestVersion`
  // is refreshed by validate() once the newest build has been fetched and hashed.
  const loaderState = (window.__bmmLoader = {
    loadedVersion: null,
    latestVersion: null,
    listeners: [],
  });

  function notifyLatestVersion(v) {
    loaderState.latestVersion = v;
    for (let i = 0; i < loaderState.listeners.length; i++) {
      try {
        loaderState.listeners[i](v);
      } catch (e) { /* a bad listener must never break loading */
      }
    }
  }

  // localStorage may be blocked (private mode / sandboxed frame); every access is
  // try/caught so storage can never break loading. No version -> hourly bucket.
  function getVersion() {
    try {
      return window.localStorage.getItem(VERSION_KEY);
    } catch (e) {
      return null;
    }
  }

  function setVersion(v) {
    try {
      if (v) window.localStorage.setItem(VERSION_KEY, v);
      else window.localStorage.removeItem(VERSION_KEY);
    } catch (e) { /* ignore */
    }
  }

  function injectScript(token, useCors, onError) {
    const s = document.createElement("script");
    s.src = LOADER_URL + "?v=" + token;
    s.async = true;
    if (useCors) s.crossOrigin = "anonymous";
    if (onError) s.onerror = onError;
    (document.head || document.documentElement).appendChild(s);
  }

  // Default load: the stable ?v=<hash> URL is served from the HTTP cache on
  // repeat visits. If it fails to load, drop the (possibly bad) pin and retry
  // once from a fresh URL without CORS so a classic cross-origin script can run.
  function load() {
    const pinned = getVersion();
    // A pinned hash means we may be booting a (possibly stale) cached build; no pin
    // means a fresh bucket load whose content is current, so report no loaded version.
    loaderState.loadedVersion = pinned;
    loaderState.latestVersion = pinned;
    const token = pinned || "t" + Math.floor(Date.now() / 3600000);
    injectScript(token, true, function () {
      setVersion(null);
      loaderState.loadedVersion = null;
      injectScript("r" + Date.now(), false, null);
    });
  }

  // Dependency-free 53-bit hash (cyrb53) — no crypto.subtle / secure context.
  function hash(str) {
    let h1 = 0xdeadbeef, h2 = 0x41c6ce57;
    for (let i = 0; i < str.length; i++) {
      const c = str.charCodeAt(i);
      h1 = Math.imul(h1 ^ c, 2654435761);
      h2 = Math.imul(h2 ^ c, 1597334677);
    }
    h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507);
    h1 ^= Math.imul(h2 ^ (h2 >>> 13), 3266489909);
    h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507);
    h2 ^= Math.imul(h1 ^ (h1 >>> 13), 3266489909);
    return (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(36);
  }

  // Best-effort update check: re-fetch the canonical loader (cache:"no-cache" =>
  // a cheap 304 when unchanged), hash the body (ETag isn't exposed cross-origin),
  // and repoint the version if it changed so the NEXT open loads the new build.
  // Never throws, never blocks loading.
  function validate() {
    if (typeof fetch !== "function") return;
    fetch(LOADER_URL, {mode: "cors", credentials: "omit", cache: "no-cache"})
      .then(function (r) {
        return r.ok ? r.text() : null;
      })
      .then(function (t) {
        if (t) {
          const v = hash(t);
          notifyLatestVersion(v);
          if (v !== getVersion()) setVersion(v);
        }
      })
      .catch(function () { /* offline / blocked: keep cached version */
      });
  }

  try {
    Object.defineProperty(window, 'bcModSdk', {
      configurable: true,
      enumerable: true,
      get: function () { return undefined; },
      set: function () { /* reserved for bmm */ },
    });
  } catch (e) { /* already defined — best effort */ }

  try {
    load();
  } catch (e) {
    injectScript("t" + Math.floor(Date.now() / 3600000), false, null);
  }
  setTimeout(validate, 1500);
})();
)SBCJS";

constexpr const char* kFusamSource = R"SBCJS(// ==UserScript==
// @name Fantastic Ultimate Solution to Addon Management Loader
// @namespace https://www.bondageprojects.com/
// @version 1.0.5
// @description A loader for the FUSAM
// @author Sidious
// @downloadURL https://sidiousious.gitlab.io/bc-addon-loader/loader.user.js
// @match https://*.bondageprojects.elementfx.com/R*/*
// @match https://*.bondage-europe.com/R*/*
// @match https://*.bondageprojects.com/R*/*
// @match https://*.bondage-asia.com/club/R*
// @match http://localhost:*/*
// @icon data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==
// @grant none
// @run-at document-end
// ==/UserScript==
/* eslint-disable no-inline-comments */
// @ts-check
// eslint-disable-next-line
/// <reference path="./node_modules/@total-typescript/ts-reset/dist/recommended.d.ts"/>

/**
 *     FUSAM Loader
 *  Copyright (C) 2023  Sid
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import(`https://sidiousious.gitlab.io/bc-addon-loader/fusam.js?v=${(Date.now() / 10000).toFixed(0)}`)
)SBCJS";

}  // namespace

std::vector<nlohmann::json> builtin_userscripts() {
    return {
        nlohmann::json{
            {"id", "builtin-bmm"},
            {"name", "BC Mod Manager"},
            {"source", kBmmSource},
            {"enabled", true},
            {"autoUpdate", true},
            {"builtin", true},
            {"downloadURL", "https://bondage-studio.github.io/bc-mod-manager/bmm.user.js"},
            {"version", "1.1"},
            {"sortOrder", 0},
        },
        nlohmann::json{
            {"id", "builtin-fusam"},
            {"name", "FUSAM"},
            {"source", kFusamSource},
            {"enabled", false},
            {"autoUpdate", true},
            {"builtin", true},
            {"downloadURL", "https://sidiousious.gitlab.io/bc-addon-loader/loader.user.js"},
            {"version", "1.0.5"},
            {"sortOrder", 1},
        },
    };
}

}  // namespace sbc::server
