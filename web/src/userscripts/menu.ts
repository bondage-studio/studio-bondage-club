// Registry of GM_registerMenuCommand entries, shared via a window global so the
// React config panel (same JS realm) can list and invoke them. An EventTarget
// notifies the panel of changes so its Menu Commands section stays live.

export interface MenuCommand {
  id: string;
  scriptId: string;
  scriptName: string;
  label: string;
  callback: (event?: Event) => void;
  accessKey?: string;
}

interface MenuRegistry {
  commands: Map<string, MenuCommand>;
  events: EventTarget;
  seq: number;
}

declare global {
  interface Window {
    __studioUserscriptMenu?: MenuRegistry;
  }
}

export function getMenuRegistry(): MenuRegistry {
  let reg = window.__studioUserscriptMenu;
  if (!reg) {
    reg = { commands: new Map(), events: new EventTarget(), seq: 0 };
    window.__studioUserscriptMenu = reg;
  }
  return reg;
}

function emitChange() {
  getMenuRegistry().events.dispatchEvent(new Event("change"));
}

export function registerMenuCommand(
  scriptId: string,
  scriptName: string,
  label: string,
  callback: (event?: Event) => void,
  accessKey?: string,
): string {
  const reg = getMenuRegistry();
  const id = `${scriptId}:${reg.seq++}`;
  reg.commands.set(id, { id, scriptId, scriptName, label, callback, accessKey });
  emitChange();
  return id;
}

export function unregisterMenuCommand(id: string): void {
  const reg = getMenuRegistry();
  if (reg.commands.delete(id)) emitChange();
}

export function clearScriptMenuCommands(scriptId: string): void {
  const reg = getMenuRegistry();
  let changed = false;
  for (const [id, cmd] of reg.commands) {
    if (cmd.scriptId === scriptId) {
      reg.commands.delete(id);
      changed = true;
    }
  }
  if (changed) emitChange();
}

export function listMenuCommands(): MenuCommand[] {
  return [...getMenuRegistry().commands.values()];
}

export function onMenuChange(listener: () => void): () => void {
  const reg = getMenuRegistry();
  reg.events.addEventListener("change", listener);
  return () => reg.events.removeEventListener("change", listener);
}
