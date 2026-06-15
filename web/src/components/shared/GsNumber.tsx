import { Input } from "../ui/input";
import { FormField } from "./FormField";
import type { AppConfig, GameServerSettings } from "../../types";

export function GsNumber({
  label,
  cfg,
  k,
  set,
}: {
  label: string;
  cfg: GameServerSettings;
  k: keyof GameServerSettings;
  set: (mutator: (draft: AppConfig) => void) => void;
}) {
  return (
    <FormField label={label}>
      <Input
        type="number"
        min={1}
        value={cfg[k]}
        onChange={(e) => set((d) => void (d.gameServerSettings[k] = Number(e.target.value)))}
      />
    </FormField>
  );
}
