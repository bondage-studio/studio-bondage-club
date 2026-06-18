import { useState } from "react";
import { Input } from "@/components/ui/input";

const DEFAULT_CODES = "200 204 404";

function parseCodes(text: string): number[] {
  return text
    .split(/[\s,]+/)
    .filter(Boolean)
    .map((t) => Number(t))
    .filter((n) => Number.isInteger(n) && n >= 100 && n <= 599);
}

export function StatusCodesField({
  value,
  onChange,
  placeholder,
}: {
  value: number[] | undefined;
  onChange: (codes: number[]) => void;
  /** Hint shown when empty; defaults to the built-in "200 204 404 (default)". */
  placeholder?: string;
}) {
  const incoming = (value ?? []).join(" ");
  const [text, setText] = useState(incoming);
  // Adopt external changes (config load / reset / remote edit) during render only
  // when they differ from what's already typed, so in-progress edits (a half-typed
  // code or a trailing space) aren't clobbered by the parsed round-trip. This is
  // React's "adjust state on prop change" pattern — no effect, no ref.
  const [prevIncoming, setPrevIncoming] = useState(incoming);
  if (incoming !== prevIncoming) {
    setPrevIncoming(incoming);
    if (parseCodes(text).join(" ") !== incoming) setText(incoming);
  }

  return (
    <Input
      value={text}
      placeholder={placeholder ?? `${DEFAULT_CODES} (default)`}
      spellCheck={false}
      inputMode="numeric"
      onChange={(e) => {
        setText(e.target.value);
        onChange(parseCodes(e.target.value));
      }}
    />
  );
}
