import { memo, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { glowAlpha, legendTint } from "../lib/colour";
import { effectById, previewFrame, rearAnimates, speedStep } from "../lib/effects";
import { KEYS, STRIP, UNITS_X, UNITS_Y } from "../lib/keyboard-data";
import type { Rgb } from "../lib/types";
import { DIRECT } from "../lib/types";
import { USAGE, usageLegend } from "../lib/usages";
import { useStore } from "../state/store";

type Props = {
  layer?: "base" | "fn";
  /** Show the rear strip under the board. */
  strip?: boolean;
  /** Keymap pages are read-only inspectors. */
  readOnly?: boolean;
  /** No LED preview - the board stays monochrome so legends stay legible. */
  mono?: boolean;
  /** Offer the Paint tool.  Only the Lighting page owns the per-key picture;
   *  on the other pages painting would silently change the effect. */
  paintable?: boolean;
};

/* Layout is identical for every render, so it is computed once instead of
 * rebuilding 63 style objects on each preview frame. */
const KEY_BOX = new Map<number, React.CSSProperties>();
for (const k of KEYS) {
  KEY_BOX.set(k.led, {
    left: `${(k.x / UNITS_X) * 100}%`,
    top: `${(k.y / UNITS_Y) * 100}%`,
    width: `${(k.w / UNITS_X) * 100}%`,
    height: `${(k.h / UNITS_Y) * 100}%`,
  });
}

const BLACK: Rgb = [0, 0, 0];

/** Compare two preview frames without allocating.  A static effect redraws
 *  only when something actually changed, so selection and status updates do
 *  not force 63 key components through render. */
function sameFrame(a: Rgb[], b: Rgb[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < b.length; i++) {
    const x = a[i];
    const y = b[i];
    if (!x || x[0] !== y[0] || x[1] !== y[1] || x[2] !== y[2]) return false;
  }
  return true;
}

type CellProps = {
  led: number;
  box: React.CSSProperties;
  selected: boolean;
  colour: string;
  glow: number;
  tint: string | undefined;
  text: string;
  wide: boolean;
  /** The key no longer sends its printed legend (see the Keymap page). */
  remapped: boolean;
};

/* One key.  Every prop is a primitive or a stable object, so React.memo can
 * skip keys whose colour and selection did not change. */
const Cell = memo(function Cell({
  led,
  box,
  selected,
  colour,
  glow,
  tint,
  text,
  wide,
  remapped,
}: CellProps) {
  return (
    <div
      className="key"
      style={box}
      data-led={led}
      data-selected={selected}
      data-remap={remapped}
      aria-pressed={selected}
    >
      <div className="key__face">
        <div
          className="key__glow"
          style={
            {
              ["--k" as string]: colour,
              ["--g" as string]: glow,
            } as React.CSSProperties
          }
        />
        <div
          className="key__legend"
          data-wide={wide}
          style={tint ? { color: tint } : undefined}
        >
          {text === "—" ? "" : text}
        </div>
      </div>
    </div>
  );
});

export function Keyboard({
  layer = "base",
  strip = true,
  readOnly = false,
  mono = false,
  paintable = false,
}: Props) {
  const { draft, selection, select, paint, brush, tool, setTool } = useStore();
  /* A paint tool left selected on the Lighting page must not follow the user
   * to the Keymap or Effects page, where a click is meant to inspect a key. */
  const activeTool = paintable ? tool : "select";
  const fx = effectById(draft.effect);
  const [frame, setFrame] = useState<Rgb[]>(() => previewFrame(draft, 0));
  const tRef = useRef(0);
  const fadesRef = useRef(new Map<number, number>());
  const drag = useRef<{ mode: "add" | "paint" } | null>(null);
  const hover = useRef<number | null>(null);
  const draftRef = useRef(draft);
  draftRef.current = draft;

  const selected = useMemo(() => new Set(selection), [selection]);
  const animating = !mono && (fx.usesSpeed || rearAnimates(draft));

  /* Reactive rear mode flares on every key press, whatever the key effect is,
   * because led_key_event() sets led_back_fade unconditionally. */
  const rearReactive = draft.backOn !== 0 && draft.backMode === 5;
  const flash = (led: number) => {
    if (fx.reactive || rearReactive) fadesRef.current.set(led, 255);
  };

  /* Frames are only published when they differ, so a solid or direct frame
   * costs no render at all once it has settled. */
  const draw = useCallback(() => {
    const fades: Record<number, number> = {};
    for (const [led, v] of fadesRef.current) if (v > 0) fades[led] = Math.round(v);
    const next = previewFrame(draftRef.current, tRef.current, fades);
    setFrame((prev) => (sameFrame(prev, next) ? prev : next));
  }, []);

  useEffect(() => {
    if (!animating) {
      draw();
      return;
    }

    let raf = 0;
    let last = 0;

    const loop = (now: number) => {
      raf = requestAnimationFrame(loop);
      if (now - last < 22) return; /* ~45 fps is plenty for a preview */
      last = now;
      /* The firmware advances led_t by led_speed once per published frame
       * (~89 Hz).  The preview runs at ~45 Hz, so it advances twice as far to
       * match the real animation rate on screen. */
      tRef.current = (tRef.current + speedStep(draftRef.current.speed) * 2) & 0xffff;
      if (fadesRef.current.size) {
        for (const [led, v] of fadesRef.current) {
          const next = v - 7;
          if (next <= 0) fadesRef.current.delete(led);
          else fadesRef.current.set(led, next);
        }
      }
      draw();
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [draw, animating]);

  /* Non-animating effects still have to follow every edit.  The animation
   * loop reads draftRef itself, so it does not need to be restarted. */
  useEffect(() => {
    if (!animating) draw();
  }, [draft, draw, animating]);

  useEffect(() => {
    const end = () => {
      drag.current = null;
      hover.current = null;
    };
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
    return () => {
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
    };
  }, []);

  const ledAt = (target: EventTarget | null): number | null => {
    const el = (target as HTMLElement | null)?.closest?.("[data-led]") as HTMLElement | null;
    if (!el) return null;
    const led = Number(el.dataset.led);
    return Number.isFinite(led) ? led : null;
  };

  const onDown = (e: React.PointerEvent) => {
    if (readOnly || e.button !== 0) return;
    const led = ledAt(e.target);
    if (led === null) {
      /* empty space inside the plate clears the selection */
      if (activeTool === "select") select([]);
      return;
    }
    e.preventDefault();
    /* Reactive effects light the key now and let the preview decay it; the
     * firmware does the same from led_fade on the next real keypress. */
    flash(led);

    if (activeTool === "paint") {
      paint([led], brush);
      drag.current = { mode: "paint" };
      return;
    }
    const additive = e.shiftKey;
    const toggle = e.metaKey || e.ctrlKey;
    if (toggle) select([led], "toggle");
    else if (additive) select([led], "add");
    else select([led], "replace");
    drag.current = { mode: "add" };
  };

  const onOver = (e: React.PointerEvent) => {
    if (readOnly) return;
    const led = ledAt(e.target);
    if (led === null || led === hover.current) return;
    hover.current = led;
    flash(led);
    const d = drag.current;
    if (!d) return;
    if (d.mode === "paint") paint([led], brush);
    else if (!selected.has(led)) select([led], "add");
  };

  return (
    <>
      <div className="board-wrap">
        <div className="board" data-tool={activeTool} onPointerDown={onDown}>
          <div className="board__grid" />
          <div
            className="board__keys"
            onPointerOver={onOver}
            onPointerLeave={() => {
              hover.current = null;
            }}
          >
            {KEYS.map((k) => {
              const rgb = (mono ? BLACK : (frame[k.led] ?? BLACK)) as Rgb;
              const glow = mono ? 0 : glowAlpha(rgb);
              /* Only the base layer can be remapped, and the picture follows
               * the board: a remapped key shows what it actually sends. */
              const assigned = draft.remap[k.led] ?? USAGE.FACTORY;
              const remapped = layer === "base" && assigned !== USAGE.FACTORY;
              return (
                <Cell
                  key={k.id}
                  led={k.led}
                  box={KEY_BOX.get(k.led)!}
                  selected={selected.has(k.led)}
                  colour={`rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})`}
                  glow={glow}
                  tint={glow > 0.02 ? legendTint(rgb) : undefined}
                  text={layer === "fn" ? k.fn : remapped ? usageLegend(assigned) : k.legend}
                  wide={k.w > 1.3}
                  remapped={remapped}
                />
              );
            })}
          </div>
        </div>
      </div>

      {strip && (
        <div className="strip">
          <span className="micro strip__label">Rear light</span>
          <div className="strip__segments">
            {STRIP.map((s) => {
              const rgb = frame[s.led] ?? BLACK;
              return (
                <div
                  key={s.id}
                  className="strip__seg"
                  style={{ background: `rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})` }}
                  title={`${s.id} · LED ${s.led}`}
                />
              );
            })}
          </div>
          <span className="micro" style={{ color: "var(--color-faint)" }}>
            {draft.backOn ? `${Math.round((draft.backLevel / 127) * 100)}%` : "off"}
          </span>
        </div>
      )}

      {!readOnly && paintable && (
        <div className="mt-3 flex items-center justify-between gap-3">
          <div className="flex items-center gap-2">
            <span className="micro">Tool</span>
            <div className="seg" style={{ width: 148 }}>
              <button
                type="button"
                className="seg__btn"
                data-active={activeTool === "select"}
                onClick={() => setTool("select")}
              >
                Select
              </button>
              <button
                type="button"
                className="seg__btn"
                data-active={activeTool === "paint"}
                onClick={() => setTool("paint")}
              >
                Paint
              </button>
            </div>
            <span className="field__hint">
              {draft.effect === 34
                ? "drag to paint the Custom picture"
                : activeTool === "paint"
                  ? "drag to paint the brush colour"
                  : "drag to select · shift adds · cmd toggles"}
            </span>
          </div>
          <div className="flex items-center gap-3">
            <span className="field__hint">Selected</span>
            <span className="value">
              {selection.length ? String(selection.length).padStart(2, "0") : "00"}
              <span className="value--dim"> / 63</span>
            </span>
          </div>
        </div>
      )}
    </>
  );
}
