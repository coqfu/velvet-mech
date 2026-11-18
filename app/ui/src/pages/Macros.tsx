import { useEffect, useMemo, useRef, useState } from "react";
import { PageFrame } from "../components/PageFrame";
import { KV, Section } from "../components/ui";
import { KEYS } from "../lib/keyboard-data";
import type { MacroStep } from "../lib/types";
import { usageFromCode, usageName } from "../lib/usages";
import { useStore } from "../state/store";

const SLOTS = [0, 1, 2, 3];
const MAX_STEPS = 24;

const keyName = (index: number) =>
  index === 0xff ? "Unbound" : KEYS[index]?.id ?? `Key ${index}`;

export function MacrosPage() {
  const { draft, saveMacro, toast, connected } = useStore();
  const [slot, setSlot] = useState(0);
  const [trigger, setTrigger] = useState(0xff);
  const [steps, setSteps] = useState<MacroStep[]>([]);
  const [recording, setRecording] = useState(false);
  const [dirty, setDirty] = useState(false);
  const down = useRef(new Map<string, number>());

  const macros = draft.macros;

  /* Adopt the slot from the board whenever the page loads or the slot changes,
   * but never overwrite a recording in progress after a background poll. */
  useEffect(() => {
    const m = macros[slot];
    setTrigger(m?.key ?? 0xff);
    setSteps((m?.steps ?? []).map((s) => ({ ...s })));
    setDirty(false);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [slot]);

  const storedSig = `${macros[slot]?.key ?? 0xff}:${macros[slot]?.len ?? 0}`;
  const sigRef = useRef(storedSig);
  useEffect(() => {
    if (sigRef.current === storedSig) return;
    sigRef.current = storedSig;
    if (recording) return;
    const m = macros[slot];
    setTrigger(m?.key ?? 0xff);
    setSteps((m?.steps ?? []).map((s) => ({ ...s })));
    setDirty(false);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [storedSig]);

  /* Recorder: keydown/keyup in the browser, one step per completed press. */
  useEffect(() => {
    if (!recording) return;

    const stop = () => {
      setRecording(false);
      down.current.clear();
    };

    const onDown = (e: KeyboardEvent) => {
      if (e.repeat) return;
      if (e.code === "Escape") {
        e.preventDefault();
        stop();
        return;
      }
      if (!usageFromCode(e.code)) return;
      e.preventDefault();
      down.current.set(e.code, performance.now());
    };

    const onUp = (e: KeyboardEvent) => {
      const started = down.current.get(e.code);
      if (started === undefined) return;
      down.current.delete(e.code);
      const usage = usageFromCode(e.code);
      if (!usage) return;
      const hold = Math.max(1, Math.min(255, Math.round((performance.now() - started) / 10)));
      setSteps((cur) => {
        if (cur.length >= MAX_STEPS) return cur;
        return [...cur, { usage, hold }];
      });
      setDirty(true);
    };

    window.addEventListener("keydown", onDown);
    window.addEventListener("keyup", onUp);
    window.addEventListener("blur", stop);
    return () => {
      window.removeEventListener("keydown", onDown);
      window.removeEventListener("keyup", onUp);
      window.removeEventListener("blur", stop);
    };
  }, [recording]);

  useEffect(() => {
    if (recording && steps.length >= MAX_STEPS) setRecording(false);
  }, [recording, steps.length]);

  const totalMs = useMemo(
    () => steps.reduce((n, s) => n + Math.max(10, s.hold * 10) + 15, 0),
    [steps]
  );

  /* The board fires the lowest-numbered slot bound to a key, so a second
   * macro on the same trigger would simply never play.  Warn before the user
   * stores something that looks armed but is not. */
  const conflict =
    trigger !== 0xff &&
    macros.some((m, i) => i !== slot && m.len > 0 && m.key === trigger);

  const commit = async () => {
    if (trigger === 0xff) {
      toast("Choose the key that should fire this macro", "warn");
      return;
    }
    /* Only clear the unsaved marker when the board accepted the slot.  A
     * failed write used to leave "Store on keyboard" disabled with the
     * sequence still on screen, so the only way to retry was to touch
     * something else. */
    if (await saveMacro(slot, trigger, steps)) setDirty(false);
  };

  const clearSlot = async () => {
    if (!(await saveMacro(slot, 0xff, []))) return;
    setTrigger(0xff);
    setSteps([]);
    setDirty(false);
  };

  return (
    <PageFrame wide>
      <div className="wide-body">
        <div className="wide-inner">
          <div className="page-head">
            <div>
              <h1 className="title">Macros</h1>
              <p className="subtitle">
                Four slots, 24 steps each. The app records, the keyboard plays back on its own.
              </p>
            </div>
            <span className="micro">
              {macros.filter((m) => m.len > 0).length} / 4 slots in use
            </span>
          </div>

          <div className="wide-grid">
            <div className="box">
              <div className="box__head">
                <span className="section__name">Slots</span>
                <span className="section__aside">stored in data flash 0xF180</span>
              </div>
              <div className="box__body">
                <div className="slot-list">
                  {SLOTS.map((i) => {
                    const m = macros[i];
                    return (
                      <button
                        key={i}
                        type="button"
                        className="slot-row"
                        data-active={slot === i}
                        onClick={() => setSlot(i)}
                      >
                        <span className="slot-row__no">M{i + 1}</span>
                        <span className="slot-row__body">
                          <span className="slot-row__name">{keyName(m?.key ?? 0xff)}</span>
                          <span className="slot-row__meta">
                            {m?.len
                              ? `${m.len} steps · ${m.steps.reduce(
                                  (n, s) => n + Math.max(10, s.hold * 10) + 15,
                                  0
                                )} ms`
                              : "empty"}
                          </span>
                        </span>
                      </button>
                    );
                  })}
                </div>
                <div className="mt-4">
                  <KV
                    rows={[
                      ["Storage", "4 × 24 steps × 2 bytes"],
                      ["Step", "HID usage + hold in 10 ms units"],
                      ["Gap", "15 ms release between steps"],
                      ["Playback", "on the board, no host needed"],
                    ]}
                  />
                </div>
              </div>
            </div>

            <div className="box">
              <div className="box__head">
                <span className="section__name">Recorder · M{slot + 1}</span>
                <span className="section__aside">
                  {steps.length} / {MAX_STEPS} steps
                </span>
              </div>
              <div className="box__body">
                <div className="flex items-center gap-3" style={{ flexWrap: "wrap" }}>
                  <label className="field__label" htmlFor="macro-trigger">
                    Trigger key
                  </label>
                  <select
                    id="macro-trigger"
                    className="input"
                    style={{ maxWidth: 220 }}
                    value={trigger}
                    onChange={(e) => {
                      setTrigger(Number(e.target.value));
                      setDirty(true);
                    }}
                  >
                    <option value={0xff}>Unbound</option>
                    {/* Fn is a layer modifier; a macro there would fire on
                        every lighting chord, so the board rejects it too. */}
                    {KEYS.map((k, i) =>
                      k.id === "FN" ? null : (
                        <option key={k.id} value={i}>
                          {k.id}
                        </option>
                      )
                    )}
                  </select>

                  <button
                    className={`btn btn--sm${recording ? " btn--primary" : ""}`}
                    onClick={() => {
                      if (recording) setRecording(false);
                      else {
                        setSteps([]);
                        setDirty(true);
                        setRecording(true);
                      }
                    }}
                  >
                    {recording ? "Stop recording" : steps.length ? "Record again" : "Record"}
                  </button>
                  <button
                    className="chip"
                    disabled={!steps.length}
                    onClick={() => {
                      setSteps((s) => s.slice(0, -1));
                      setDirty(true);
                    }}
                  >
                    Undo step
                  </button>
                  <button
                    className="chip"
                    disabled={!steps.length}
                    onClick={() => {
                      setSteps([]);
                      setDirty(true);
                    }}
                  >
                    Clear
                  </button>
                </div>

                <p className="field__hint" style={{ marginTop: 10, lineHeight: 1.6 }}>
                  {recording
                    ? "Recording — type the sequence. Esc stops. Every completed press becomes one step."
                    : "Press Record, then type. Holds are measured in 10 ms ticks; a tap becomes one tick."}
                </p>
                {conflict && (
                  <p
                    className="field__hint"
                    style={{ marginTop: 8, lineHeight: 1.6, color: "var(--color-danger)" }}
                  >
                    Another slot already fires on {keyName(trigger)}. The board plays the
                    lower-numbered slot, so clear or move that one before storing this.
                  </p>
                )}

                <div className="step-list">
                  {steps.length === 0 ? (
                    <div className="empty" style={{ padding: "18px 12px" }}>
                      <div className="empty__title">No steps</div>
                      <div className="empty__body">
                        {macros[slot]?.len
                          ? "The slot is loaded; Record replaces its sequence."
                          : "This slot is empty. Record a sequence to fill it."}
                      </div>
                    </div>
                  ) : (
                    steps.map((s, i) => (
                      <div className="step-row" key={`${s.usage}-${i}`}>
                        <span className="step-row__no">{String(i + 1).padStart(2, "0")}</span>
                        <span className="step-row__key">{usageName(s.usage)}</span>
                        <span className="step-row__hold">
                          {Math.max(10, s.hold * 10)} ms
                        </span>
                        <button
                          className="chip"
                          onClick={() => {
                            setSteps((cur) => cur.filter((_, j) => j !== i));
                            setDirty(true);
                          }}
                        >
                          ×
                        </button>
                      </div>
                    ))
                  )}
                </div>

                <div className="mt-4 flex items-center gap-2" style={{ flexWrap: "wrap" }}>
                  <button
                    className="btn btn--primary"
                    disabled={!connected || trigger === 0xff || !dirty}
                    onClick={() => void commit()}
                    title={!connected ? "The keyboard is not connected" : undefined}
                  >
                    Store on keyboard
                  </button>
                  <button
                    className="btn btn--quiet"
                    disabled={!connected}
                    onClick={() => void clearSlot()}
                  >
                    Clear slot
                  </button>
                  <span className="field__hint" style={{ marginLeft: "auto" }}>
                    {totalMs} ms total
                  </span>
                </div>
                {!connected && (
                  <p className="field__hint" style={{ marginTop: 8 }}>
                    Connect the keyboard to write the slot; recording works offline.
                  </p>
                )}
              </div>
            </div>
          </div>

          <div className="mt-4">
            <Section title="What the board really does" aside="firmware · src/macro.c">
              <p className="field__hint" style={{ lineHeight: 1.7 }}>
                A macro is a step list, not a keystroke spy. The app records the presses it sees in
                the browser and uploads the list over raw HID; the board replays it locally from its
                own timer, one step at a time, with a 15 ms release gap so repeated keys register as
                separate presses. One macro runs at a time. A key with a macro bound types nothing
                of its own: the steps replace that key, and stay replaced even after the list has
                finished while the switch is still held. Clear the slot, or bind it to another key,
                to get the key's normal character and remap back. Fn is not offered as a trigger:
                it is the layer key, so a macro bound there would fire on every lighting chord.
              </p>
            </Section>
          </div>
        </div>
      </div>
    </PageFrame>
  );
}
