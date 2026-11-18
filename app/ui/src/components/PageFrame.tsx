import type { ReactNode } from "react";
import { useStore } from "../state/store";

export function PageFrame({ children, wide }: { children: ReactNode; wide?: boolean }) {
  return <div className={`workspace${wide ? " workspace--wide" : ""}`}>{children}</div>;
}

export function Stage({ children }: { children: ReactNode }) {
  return <section className="stage">{children}</section>;
}

export function StageBody({ children, top }: { children: ReactNode; top?: boolean }) {
  return <div className={`stage__body${top ? " stage__body--top" : ""}`}>{children}</div>;
}

export function StageFoot({ children }: { children: ReactNode }) {
  return <div className="stage__foot">{children}</div>;
}

export function Panel({ children, foot }: { children: ReactNode; foot?: ReactNode }) {
  return (
    <aside className="panel">
      <div className="panel__scroll">{children}</div>
      {foot && <div className="panel__foot">{foot}</div>}
    </aside>
  );
}

/** REVERT + APPLY, with the unapplied-changes indicator. */
export function ApplyBar({ note }: { note?: string }) {
  const { apply, revert, dirty, busy, connected, draft } = useStore();
  return (
    <>
      <div className="flex items-center gap-2" style={{ flex: 1, minWidth: 0 }}>
        <span className="dot" data-state={dirty ? "warn" : "ok"} />
        <span className="micro" style={{ color: dirty ? "var(--color-warn)" : undefined }}>
          {dirty ? "Unapplied" : "In sync"}
        </span>
        {note && (
          <span
            className="field__hint"
            style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}
          >
            {note}
          </span>
        )}
      </div>
      <button className="btn btn--quiet" onClick={revert} disabled={!dirty || busy} title="Discard changes">
        Revert
      </button>
      <button
        className="btn btn--primary"
        onClick={() => void apply()}
        disabled={busy || !connected || !dirty}
        title={
          !connected
            ? "The keyboard is not connected"
            : dirty
              ? `Write ${draft.effect === -1 ? "the Direct frame" : "the effect"} to the keyboard`
              : "Nothing to apply"
        }
      >
        {busy ? "Writing…" : "Apply"}
      </button>
    </>
  );
}
