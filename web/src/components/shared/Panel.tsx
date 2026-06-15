import type { ReactNode } from "react";
import { cn } from "@/lib/utils";

export function Panel({
  title,
  action,
  children,
  className,
}: {
  title?: ReactNode;
  action?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <section
      className={cn("overflow-hidden rounded-md border bg-card @max-xl:shadow-md", className)}
    >
      {title && (
        <div className="flex h-7 items-center justify-between gap-2 border-b bg-muted px-3">
          <h3 className="text-xs font-semibold uppercase tracking-wide text-muted-foreground">
            {title}
          </h3>
          {action}
        </div>
      )}
      <div className="p-3">{children}</div>
    </section>
  );
}
