import type { ReactNode } from "react";
import { Label } from "@/components/ui/label";

export function FormField({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="grid gap-1">
      <Label className="text-xs text-muted-foreground">{label}</Label>
      {children}
    </div>
  );
}
