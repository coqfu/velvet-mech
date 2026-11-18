import { useEffect, useState } from "react";
import { PageFrame, Panel, Stage, StageBody, StageFoot } from "../components/PageFrame";
import { KV, Metric, Section, Slider, StageHead, Toggle } from "../components/ui";
import { api } from "../lib/api";
import { clamp } from "../lib/colour";
import type { Diag, Rgb } from "../lib/types";
import { useStore } from "../state/store";

const FRAME_MS = 11.2; /* one published frame at ~89 Hz */

function useDiag(active: boolean): { diag: Diag | null; fpsHistory: number[] } {
  const [diag, setDiag] = useState<Diag | null>(null);
  const [fpsHistory, setFpsHistory] = useState<number[]>([]);
  useEffect(() => {
    if (!active) {
      /* Nothing to ask: the service answers null without a board, and the
       * panel should read "no reply" rather than keep polling. */
      setDiag(null);
      setFpsHistory([]);
      return;
    }
    let alive = true;
    const tick = async () => {
      try {
        const d = await api.diag();
        if (!alive) return;
        setDiag(d);
        if (d) {
          const fps = d.fps;
          if (fps !== null) {
            setFpsHistory((cur) => [...cur.slice(-59), fps]);
          } else {
            /* The frame counter restarted with the board: start the trace
             * over rather than join the two runs into one line. */
            setFpsHistory([]);
          }
        }
      } catch {
        if (alive) setDiag(null);
      }
    };
    void tick();
    const id = window.setInterval(() => void tick(), 1000);
    return () => {
      alive = false;
      window.clearInterval(id);
    };
  }, [active]);
  return { diag, fpsHistory };
}

/** Rolling frame-rate trace, one sample per second.
 *
 * The axis is a fixed 80-100 fps window: the panel idles at 89, so anything
 * below that is a real dip.  An auto-fitting axis would rescale every second
 * and turn 1 fps of jitter into fake drama. */
function FrameTrace({ samples }: { samples: number[] }) {
  const W = 100;
  const H = 40;
  const lo = 80;
  const hi = 100;
  const yOf = (v: number) => H - ((Math.max(lo, Math.min(hi, v)) - lo) / (hi - lo)) * H;
  const xOf = (i: number, n: number) => (n <= 1 ? W : (i / (n - 1)) * W);
  const points = samples.map((v, i) => `${xOf(i, samples.length).toFixed(2)},${yOf(v).toFixed(2)}`);
  const area =
    points.length > 1
      ? `0,${H} ${points.join(" ")} ${W},${H}`
      : null;
  const last = samples.at(-1) ?? 0;
  return (
    <div className="trace">
      <div className="trace__head">
        <span className="micro">
          Frame rate · {samples.length} {samples.length === 1 ? "sample" : "samples"}
        </span>
        <span className="value">
          {samples.length ? last.toFixed(0) : "—"}
          <span className="value--dim"> fps</span>
        </span>
      </div>
      <svg
        className="trace__plot"
        viewBox={`0 0 ${W} ${H}`}
        preserveAspectRatio="none"
        role="img"
        aria-label="Measured frame rate"
      >
        {[85, 90, 95].map((v) => (
          <line key={v} x1="0" y1={yOf(v)} x2={W} y2={yOf(v)} className="trace__grid" />
        ))}
        <line x1="0" y1={yOf(89)} x2={W} y2={yOf(89)} className="trace__guide" />
        {area ? (
          <>
            <polygon points={area} className="trace__fill" />
            <polyline points={points.join(" ")} className="trace__line" />
          </>
        ) : (
          samples.length === 1 && <circle cx={W} cy={yOf(samples[0])} r="0.9" className="trace__dot" />
        )}
      </svg>
      <div className="trace__foot">
        <span>80</span>
        <span>target 89</span>
        <span>100</span>
      </div>
    </div>
  );
}

const CAL_PRESETS: { name: string; rgb: Rgb; note: string }[] = [
  { name: "Neutral", rgb: [128, 128, 128], note: "unity - no correction" },
  { name: "Cool", rgb: [120, 130, 150], note: "calms a warm panel" },
  { name: "Warm", rgb: [150, 128, 108], note: "calms a cool panel" },
  { name: "Reduced red", rgb: [104, 128, 128], note: "for pink-leaning reds" },
];

/** Mirrors LED_BOOT_* in include/led.h.  Every entry is implemented in
 *  render_boot_col(), so nothing here is a dead choice. */
const BOOT_MODES: { id: number; name: string; note: string }[] = [
  { id: 0, name: "Off", note: "no animation - the effect is on immediately" },
  { id: 1, name: "Sweep", note: "warm diagonal flash across the keys" },
  { id: 2, name: "Rainbow", note: "diagonal colour wipe" },
  { id: 3, name: "Fade", note: "the whole panel fades up from black" },
  { id: 4, name: "Scan", note: "a bright column travels across the board" },
  { id: 5, name: "Pulse", note: "three whole-panel pulses" },
];

const BOOT_PACES = ["Fast", "Normal", "Slow"];

/** One sample key drawn through the same calibration the panel uses. */
function swatch(rgb: Rgb, cal: Rgb): string {
  const out = rgb.map((v, i) => clamp(Math.round((v * cal[i]) / 128), 0, 255));
  return `rgb(${out.join(", ")})`;
}

export function AdvancedPage() {
  const {
    status,
    connected,
    draft,
    setConfig,
    applyConfig,
    bootPreview,
    saveFlash,
    enterBootloader,
    flash,
    flashJob,
    busy,
    toast,
  } = useStore();
  const { diag, fpsHistory } = useDiag(connected);
  const d = status?.device;

  const budget = diag ? Math.min(100, (diag.renderMs / FRAME_MS) * 100) : 0;
  const calibrated = draft.cal.some((v) => v !== 128);

  return (
    <PageFrame>
      <Stage>
        <StageHead
          title="Advanced"
          subtitle="Typing behaviour, panel calibration and firmware. Everything here maps to a real command."
          right={
            <>
              <span className="micro">Firmware</span>
              <span className="value">{d?.qmk ?? "—"}</span>
            </>
          }
        />
        <StageBody top>
          <div className="metric-grid">
            <Metric label="Frames" value={diag && diag.fps !== null ? diag.fps.toFixed(1) : "—"} unit=" fps" />
            <Metric label="Render" value={diag ? diag.renderMs.toFixed(2) : "—"} unit=" ms" />
            <Metric
              label="Frame budget"
              value={diag ? `${budget.toFixed(0)}` : "—"}
              unit=" %"
            />
            <Metric label="Last reset" value={diag?.reset ?? "—"} />
          </div>
          <div className="mt-4">
            <KV
              rows={[
                ["Device", d?.name ?? "not connected"],
                ["Protocol", d ? String(d.protocol ?? "—") : "—"],
                ["LEDs", d ? `${d.ledCount} (${d.keyCount} keys + ${d.underglowCount} rear)` : "—"],
                ["Effect", diag ? `${diag.effect} · phase ${diag.phase}` : "—"],
                ["Brightness", diag ? String(diag.brightness) : "—"],
                ["Rear light", diag ? (diag.backLight ? "on" : "off") : "—"],
                ["Frames", diag ? `${diag.frames.toLocaleString()} · 16-bit counter` : "—"],
              ]}
            />
          </div>
          <div className="trace-wrap">
            <FrameTrace samples={fpsHistory} />
          </div>
        </StageBody>
        <StageFoot>
          <span className="field__hint">
            Diagnostics are read from the board once a second over raw HID.
          </span>
          <span className="micro">
            {diag ? `${diag.frames.toLocaleString()} frames since boot` : "no reply"}
          </span>
        </StageFoot>
      </Stage>

      <Panel>
        <Section no="01" title="Typing" aside={`debounce ${draft.debounce}`}>
          <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
            <span className="field__label">Debounce</span>
            <span className="value">
              {draft.debounce}
              <span className="value--dim"> / 6 frames</span>
            </span>
          </div>
          <Slider
            min={1}
            max={6}
            value={draft.debounce}
            onChange={(v) => setConfig({ debounce: v })}
            label="Debounce"
          />
          <p className="field__hint field__hint--prose" style={{ marginTop: 6, lineHeight: 1.6 }}>
            Stable scan frames before a switch registers. 1 is the fastest and can chatter on worn
            switches; 6 is the most tolerant.
          </p>

          <div className="flex items-center justify-between" style={{ marginTop: 14 }}>
            <div>
              <span className="field__label">Gaming mode</span>
              <p className="field__hint" style={{ marginTop: 2 }}>
                Drops the left-Windows usage (0xE3) from the report, so a stray press cannot pull
                you out of a game; every other key types normally.
              </p>
            </div>
            <Toggle
              on={draft.gaming === 1}
              onChange={(v) => setConfig({ gaming: v ? 1 : 0 })}
              label="Gaming mode"
            />
          </div>

          <button
            className="btn btn--sm btn--primary"
            style={{ marginTop: 14 }}
            disabled={!connected || busy}
            onClick={() => void applyConfig()}
          >
            Write typing config
          </button>
          <p className="field__hint" style={{ marginTop: 8 }}>
            Sent as its own packet when you press this button, not with the lighting apply.
          </p>
        </Section>

        <Section no="02" title="Colour calibration" aside={calibrated ? "adjusted" : "unity"}>
          <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6, marginBottom: 12 }}>
            The panel's channels are not equally bright. Calibration multiplies every LED channel
            before the 6-bit dimmer, so it corrects the whole board rather than one frame. 128 is
            unity; lower a channel to tame it.
          </p>

          {(["R", "G", "B"] as const).map((name, i) => (
            <div key={name} style={{ marginBottom: 8 }}>
              <div className="flex items-center justify-between" style={{ marginBottom: 2 }}>
                <span className="field__label">
                  {name === "R" ? "Red" : name === "G" ? "Green" : "Blue"}
                </span>
                <span className="value">
                  {draft.cal[i]}
                  <span className="value--dim"> / 255</span>
                </span>
              </div>
              <Slider
                min={0}
                max={255}
                value={draft.cal[i]}
                onChange={(v) => {
                  const cal = [...draft.cal] as Rgb;
                  cal[i] = v;
                  setConfig({ cal });
                }}
                label={`${name} calibration`}
                variant="hue"
              />
            </div>
          ))}

          <div className="chips" style={{ marginTop: 10 }}>
            {CAL_PRESETS.map((p) => (
              <button
                key={p.name}
                className="chip"
                title={p.note}
                data-active={p.rgb.every((v, i) => v === draft.cal[i])}
                onClick={() => setConfig({ cal: [...p.rgb] as Rgb })}
              >
                {p.name}
              </button>
            ))}
          </div>

          <div className="cal-swatches">
            {([
              ["White", [255, 255, 255]],
              ["Red", [255, 0, 0]],
              ["Green", [0, 255, 0]],
              ["Blue", [0, 0, 255]],
            ] as [string, Rgb][]).map(([name, rgb]) => (
              <div key={name} className="cal-swatch">
                <span
                  className="cal-swatch__chip"
                  style={{ background: swatch(rgb, draft.cal) }}
                />
                <span className="cal-swatch__name">{name}</span>
              </div>
            ))}
          </div>

          <button
            className="btn btn--sm btn--primary"
            style={{ marginTop: 14 }}
            disabled={!connected || busy}
            onClick={() => void applyConfig()}
          >
            Write calibration
          </button>
          <button
            className="btn btn--sm btn--quiet"
            style={{ marginTop: 14, marginLeft: 6 }}
            disabled={!connected || busy || !calibrated}
            onClick={() => {
              setConfig({ cal: [128, 128, 128] });
              toast("Calibration back to unity - write it to commit");
            }}
          >
            Reset to 128
          </button>
        </Section>

        <Section no="03" title="Start-up animation" aside={BOOT_MODES[draft.bootMode]?.name ?? "off"}>
          <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6, marginBottom: 12 }}>
            What the board plays once when power reaches it: five animations, plus Off, each
            rendered by the same LED engine as the effects. The choice and pace live in the
            settings block, so they survive a power cycle. “Write and play” saves the whole
            hardware config first, then replays the animation immediately — the board shows exactly
            what it will do at power-on. With Off there is nothing to replay; the board goes
            straight to the active effect.
          </p>

          <div className="chips">
            {BOOT_MODES.map((m) => (
              <button
                key={m.id}
                className="chip"
                title={m.note}
                data-active={draft.bootMode === m.id}
                onClick={() => setConfig({ bootMode: m.id })}
              >
                {m.name}
              </button>
            ))}
          </div>

          <div style={{ marginTop: 12 }}>
            <span className="field__label">Pace</span>
            <div className="chips" style={{ marginTop: 6 }}>
              {BOOT_PACES.map((name, i) => (
                <button
                  key={name}
                  className="chip"
                  data-active={draft.bootPace === i}
                  onClick={() => setConfig({ bootPace: i })}
                >
                  {name}
                </button>
              ))}
            </div>
          </div>

          <button
            className="btn btn--sm btn--primary"
            style={{ marginTop: 14 }}
            disabled={!connected || busy}
            onClick={async () => {
              if (!(await applyConfig())) return;
              if (draft.bootMode !== 0) await bootPreview();
            }}
          >
            {draft.bootMode === 0 ? "Write (no animation)" : "Write and play"}
          </button>
          <p className="field__hint" style={{ marginTop: 8 }}>
            {draft.bootMode === 0
              ? "Off: nothing plays at power-on, and the active effect is on immediately."
              : "Runs for 0.6–1.9 s depending on the animation and pace, then the active effect takes over."}
          </p>
        </Section>

        <Section no="04" title="Firmware" aside={connected ? "connected" : "offline"}>
          <div className="flex items-center gap-2" style={{ flexWrap: "wrap" }}>
            <button
              className="btn btn--sm"
              disabled={!connected || busy}
              onClick={() => void saveFlash()}
              title="Persist effect, remap, custom picture, macros and config"
            >
              Save everything to flash
            </button>
            <button
              className="btn btn--sm btn--quiet"
              disabled={!connected || busy}
              onClick={() => void enterBootloader()}
              title="Restart the keyboard firmware (a software jump cannot hold the ROM bootloader)"
            >
              Restart keyboard
            </button>
            <button
              className="btn btn--sm btn--primary"
              disabled={busy}
              onClick={() => void flash()}
              title="Wait for the ISP bootloader, then run wchisp on build/keyboard.hex"
            >
              {flashJob.state === "waiting"
                ? "Waiting for bootloader…"
                : flashJob.state === "writing"
                  ? "Writing…"
                  : "Flash build/keyboard.hex"}
            </button>
          </div>

          {(flashJob.state === "waiting" || flashJob.state === "writing") && (
            <div className="flash-note" data-state={flashJob.state}>
              <span className="dot" data-state="warn" />
              <div>
                <div className="flash-note__title">
                  {flashJob.state === "waiting" ? "Power-cycle the keyboard" : "Writing firmware"}
                </div>
                <div className="flash-note__body">{flashJob.message}</div>
              </div>
              <span className="flash-note__time">{flashJob.waited.toFixed(0)}s</span>
            </div>
          )}

          {flashJob.state === "done" && (
            <div className="flash-note" data-state="done">
              <span className="dot" data-state="ok" />
              <div>
                <div className="flash-note__title">Written</div>
                <div className="flash-note__body">{flashJob.message}</div>
              </div>
            </div>
          )}

          {flashJob.state === "failed" && (
            <div className="flash-note" data-state="failed">
              <span className="dot" data-state="error" />
              <div>
                <div className="flash-note__title">Not written</div>
                <div className="flash-note__body">{flashJob.message}</div>
              </div>
            </div>
          )}

          <div className="mt-3">
            <KV
              rows={[
                ["Save", "settings · remap · picture · macros"],
                ["ISP device", "4348:55e0 · power-on reset only"],
                ["Bootloader at", "0xF400 · bBOOT_LOAD is read-only"],
                ["Flasher", "tools/wchisp/target/release/wchisp"],
                ["Image", "build/keyboard.hex"],
              ]}
            />
          </div>

          <p className="field__hint field__hint--prose" style={{ marginTop: 10, lineHeight: 1.7 }}>
            The keyboard cannot enter its bootloader from software: the CH555 bootloader reads a
            read-only <code className="kbd">bBOOT_LOAD</code> bit that only survives a power-on
            reset, so a jump to <code className="kbd">0xF400</code> falls straight back into the
            firmware. Flash therefore takes over everything except the one thing only you can do:
            start it, fit the boot jumper, then power-cycle the board — unplug and replug the
            cable, so the chip sees a real power-on reset with the jumper in place. The job polls
            for <code className="kbd">4348:55e0</code> while you do it, and the moment it appears
            it writes the image and the board restarts as a keyboard. Starting the job first
            matters: if nothing is listening, the loader times out and boots the firmware, which
            looks like "replugging does nothing".
          </p>
        </Section>
      </Panel>
    </PageFrame>
  );
}
