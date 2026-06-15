import { javascript } from "@codemirror/lang-javascript";
import { MergeView } from "@codemirror/merge";
import { EditorState } from "@codemirror/state";
import { EditorView, placeholder as cmPlaceholder } from "@codemirror/view";
import { basicSetup } from "codemirror";
import { useEffect, useRef } from "react";
import { cn } from "../../lib/utils";
import { useShadowContainer } from "../../shadow-context";

/** Colors only — heights/layout live in styles.css (`.cm-host-fill`) since the
 *  merge wrapper sits outside the per-editor theme scope. */
const panelTheme = EditorView.theme({
  "&": {
    fontSize: "12px",
    color: "hsl(var(--foreground))",
    backgroundColor: "transparent",
  },
  "&.cm-focused": { outline: "none" },
  ".cm-scroller": {
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace",
    lineHeight: "1.6",
  },
  ".cm-gutters": {
    backgroundColor: "transparent",
    color: "hsl(var(--muted-foreground))",
    borderRight: "1px solid hsl(var(--border))",
  },
  ".cm-activeLine": { backgroundColor: "hsl(var(--muted) / 0.5)" },
  ".cm-activeLineGutter": { backgroundColor: "hsl(var(--muted) / 0.5)" },
  ".cm-cursor": { borderLeftColor: "hsl(var(--foreground))" },
  ".cm-content": { caretColor: "hsl(var(--foreground))" },
  "&.cm-focused .cm-selectionBackground, .cm-selectionBackground, .cm-content ::selection": {
    backgroundColor: "hsl(var(--accent))",
  },
});

function shadowRoot(container: HTMLElement | null): Document | ShadowRoot | undefined {
  const root = container?.getRootNode();
  return root instanceof ShadowRoot || root instanceof Document ? root : undefined;
}

const readOnlyExtensions = [EditorState.readOnly.of(true), EditorView.editable.of(false)];

interface CodeEditorProps {
  value: string;
  onChange?: (value: string) => void;
  readOnly?: boolean;
  placeholder?: string;
  autoFocus?: boolean;
  className?: string;
}

/** Editable JavaScript editor. Controlled via `value`/`onChange`. */
export function CodeEditor({
  value,
  onChange,
  readOnly,
  placeholder,
  autoFocus,
  className,
}: CodeEditorProps) {
  const container = useShadowContainer();
  const hostRef = useRef<HTMLDivElement>(null);
  const viewRef = useRef<EditorView | null>(null);
  // Keep the latest onChange without re-creating the editor on every render.
  const onChangeRef = useRef(onChange);
  useEffect(() => {
    onChangeRef.current = onChange;
  });

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;
    const view = new EditorView({
      parent: host,
      root: shadowRoot(container),
      state: EditorState.create({
        doc: value,
        extensions: [
          basicSetup,
          javascript(),
          panelTheme,
          readOnly ? readOnlyExtensions : [],
          placeholder ? cmPlaceholder(placeholder) : [],
          EditorView.updateListener.of((u) => {
            if (u.docChanged) onChangeRef.current?.(u.state.doc.toString());
          }),
        ],
      }),
    });
    viewRef.current = view;
    if (autoFocus) view.focus();
    return () => {
      view.destroy();
      viewRef.current = null;
    };
    // Mount once; external value changes are reconciled in the effect below.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Reconcile external `value` updates (e.g. "Fetch" replacing the source)
  // without clobbering the user's edits or cursor when nothing actually changed.
  useEffect(() => {
    const view = viewRef.current;
    if (!view) return;
    const current = view.state.doc.toString();
    if (value !== current) {
      view.dispatch({ changes: { from: 0, to: current.length, insert: value } });
    }
  }, [value]);

  return (
    <div
      ref={hostRef}
      className={cn(
        "cm-host-fill overflow-hidden rounded-md border border-input bg-background",
        className,
      )}
    />
  );
}

interface DiffEditorProps {
  /** Left/original side. */
  original: string;
  /** Right/modified side. */
  modified: string;
  className?: string;
}

/** Read-only side-by-side diff (current vs. incoming update). */
export function DiffEditor({ original, modified, className }: DiffEditorProps) {
  const container = useShadowContainer();
  const hostRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;
    const side = [basicSetup, javascript(), panelTheme, readOnlyExtensions];
    const view = new MergeView({
      parent: host,
      root: shadowRoot(container),
      a: { doc: original, extensions: side },
      b: { doc: modified, extensions: side },
      gutter: true,
      highlightChanges: true,
      collapseUnchanged: { margin: 3, minSize: 4 },
    });
    return () => view.destroy();
  }, [container, original, modified]);

  return (
    <div
      ref={hostRef}
      className={cn(
        "cm-host-fill overflow-hidden rounded-md border border-input bg-background",
        className,
      )}
    />
  );
}
