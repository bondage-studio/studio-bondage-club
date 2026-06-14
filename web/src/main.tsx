import React from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import { ConfirmProvider } from "./components/ui/confirm";
import { readStudioBootstrap, restoreOriginalHomepage } from "./originalPage";
import { ShadowContext } from "./shadow-context";
import styles from "./styles.css?inline";

const bootstrap = readStudioBootstrap();
void restoreOriginalHomepage(bootstrap);

const host = getOrCreateAdminHost(bootstrap?.adminRootID ?? "studio-admin-root");
const shadow = host.shadowRoot ?? host.attachShadow({ mode: "open" });
shadow.replaceChildren();

const style = document.createElement("style");
style.textContent = styles;
shadow.append(style);

// Tailwind v4 composes transforms/filters/etc. through `@property`-registered
// `--tw-*` custom properties (e.g. `translate: var(--tw-translate-x) var(--tw-translate-y)`).
// `@property` rules only register from the top-level document — browsers ignore
// them inside a shadow root — so when our whole stylesheet lives in the shadow
// DOM those properties never get their default `0`, `var()` resolves to the
// guaranteed-invalid value, and the declaration is dropped (e.g. the Switch
// thumb never translates). Re-register just the `@property` rules in the light
// DOM. They only declare unused `--tw-*` properties, so they have no visual
// effect on the host page.
//
// Registering a custom property is idempotent per name, so injecting the same
// rules once is enough even if the panel re-mounts.
let tailwindPropertiesRegistered = false;
function registerTailwindProperties(css: string) {
  if (tailwindPropertiesRegistered) {
    return;
  }
  // `@property` blocks never nest braces, so a flat match is safe.
  const rules = css.match(/@property[^{]+\{[^}]*\}/g);
  if (!rules) {
    return;
  }
  const propStyle = document.createElement("style");
  propStyle.setAttribute("data-studio-tw-properties", "");
  propStyle.textContent = rules.join("");
  document.head.append(propStyle);
  tailwindPropertiesRegistered = true;
}

registerTailwindProperties(styles);

const rootElement = document.createElement("div");
rootElement.setAttribute("data-studio-panel-root", "");
shadow.append(rootElement);

createRoot(rootElement).render(
  <React.StrictMode>
    <ShadowContext.Provider value={rootElement}>
      <ConfirmProvider>
        <App />
      </ConfirmProvider>
    </ShadowContext.Provider>
  </React.StrictMode>
);

function getOrCreateAdminHost(id: string) {
  const existing = document.getElementById(id);
  if (existing) {
    return existing;
  }
  const next = document.createElement("div");
  next.id = id;
  // The host carries the only "above the game page" z-index. It establishes a
  // top-level stacking context (zero-size, so it never blocks pointer events),
  // letting every panel/window/popover inside the shadow DOM layer naturally by
  // DOM order instead of fighting over max-int z-indexes.
  next.style.position = "relative";
  next.style.zIndex = "2147483647";
  document.body.append(next);
  return next;
}
