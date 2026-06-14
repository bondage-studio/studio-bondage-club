import { useState } from "react";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Label } from "../ui/label";
import { Window } from "../ui/window";
import type { StoreConfig } from "../../types";

interface StoreEditorProps {
  initial?: StoreConfig;
  onSave: (store: StoreConfig) => void;
  onClose: () => void;
}

const empty: StoreConfig = { name: "" };

export function StoreEditor({ initial, onSave, onClose }: StoreEditorProps) {
  const isEdit = !!initial;
  const [form, setForm] = useState<StoreConfig>(initial ?? empty);

  function set(patch: Partial<StoreConfig>) {
    setForm((f) => ({ ...f, ...patch }));
  }

  function handleSave() {
    if (!form.name.trim()) return;
    onSave(form);
  }

  return (
    <Window
      onClose={onClose}
      defaultWidth={420}
      defaultHeight={400}
      minWidth={360}
      minHeight={300}
    >
      <Window.Title>{isEdit ? "Edit store" : "Add store"}</Window.Title>

      <Window.Body className="overflow-y-auto p-4">
        <div className="grid gap-3">
          <div className="grid gap-1.5">
            <Label htmlFor="store-name">Name</Label>
            <Input
              id="store-name"
              value={form.name}
              disabled={isEdit}
              onChange={(e) => set({ name: e.target.value })}
              placeholder="assets"
              spellCheck={false}
            />
          </div>

          <div className="grid gap-1.5">
            <Label htmlFor="store-dir">
              Directory <span className="text-muted-foreground">(optional)</span>
            </Label>
            <Input
              id="store-dir"
              value={form.dir ?? ""}
              onChange={(e) => set({ dir: e.target.value || undefined })}
              placeholder="Leave empty for auto"
              spellCheck={false}
            />
          </div>

          <div className="grid grid-cols-2 gap-2">
            <div className="grid gap-1.5">
              <Label htmlFor="store-max">Max bytes</Label>
              <Input
                id="store-max"
                type="number"
                min={0}
                value={form.maxSizeBytes ?? ""}
                onChange={(e) =>
                  set({ maxSizeBytes: e.target.value ? Number(e.target.value) : undefined })
                }
                placeholder="0 = inherit"
              />
            </div>
            <div className="grid gap-1.5">
              <Label htmlFor="store-ttl">Default TTL (s)</Label>
              <Input
                id="store-ttl"
                type="number"
                min={0}
                value={form.defaultTTLSeconds ?? ""}
                onChange={(e) =>
                  set({ defaultTTLSeconds: e.target.value ? Number(e.target.value) : undefined })
                }
                placeholder="0 = inherit"
              />
            </div>
          </div>
        </div>
      </Window.Body>

      <Window.Footer>
        <footer className="flex h-10 shrink-0 items-center justify-end gap-2 border-t bg-muted px-3">
          <Button variant="outline" size="sm" onClick={onClose}>
            Cancel
          </Button>
          <Button size="sm" onClick={handleSave} disabled={!form.name.trim()}>
            {isEdit ? "Update" : "Add"}
          </Button>
        </footer>
      </Window.Footer>
    </Window>
  );
}