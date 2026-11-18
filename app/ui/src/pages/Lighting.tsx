import { useState } from "react";
import { ColourEditor } from "../components/ColourEditor";
import { EffectList } from "../components/EffectList";
import { Keyboard } from "../components/Keyboard";
import { ApplyBar, PageFrame, Panel, Stage, StageBody, StageFoot } from "../components/PageFrame";
import { Section, Slider, StageHead } from "../components/ui";
import { hsvToRgb, rgbToHsv } from "../lib/colour";
import { EFFECTS, effectById, rearAnimates, speedByte, speedStep } from "../lib/effects";
import { PRESETS } from "../lib/keysets";
import { DIRECT } from "../lib/types";
import { useStore } from "../state/store";

/** Mirrors LED_BACK_* in include/led.h.  All six rear LEDs share one signal,
 *  so each mode moves in time rather than along the strip. */
const BACK_MODES = [
  { id: 0, name: "Solid", note: "steady colour" },
  { id: 1, name: "Breathe", note: "slow symmetrical swell" },
  { id: 2, name: "Heartbeat", note: "double thump, then rest" },
  { id: 3, name: "Pulse", note: "sharp whole-panel pulses" },
  { id: 4, name: "Rainbow", note: "hue travels the full wheel" },
  { id: 5, name: "Reactive", note: "flares on every key press" },
  { id: 6, name: "Strobe", note: "hard on/off flash" },
  { id: 7, name: "Sparkle", note: "random flicker around the colour" },
  { id: 8, name: "Wave", note: "hue drifts under a rolling swell" },
];

export function LightingPage() {
  const {
    draft,
    selection,
    select,
    selectAll,
    clearSelection,
    invertSelection,
    setColour,
    setLighting,
    setEffect,
    setBrush,
    brush,
    keysets,
    saveKeyset,
    deleteKeyset,
  } = useStore();
  const [keysetName, setKeysetName] = useState("");

  const fx = effectById(draft.effect);
  const target = selection.length;
  const paintingCustom = draft.effect === 34;
  /* What the picker edits: the base colour in colour-owned effects, the
   * selection in Direct/Custom, otherwise the brush. */
  const current: [number, number, number] =
    fx.usesColour && draft.effect !== DIRECT
      ? hsvToRgb(draft.hue, draft.sat, 255)
      : paintingCustom
        ? (selection.length ? draft.custom[selection[0]] : brush)
        : selection.length
          ? draft.colours[selection[0]]
          : brush;

  const commitColour = (rgb: [number, number, number]) => {
    if (fx.usesColour && draft.effect !== DIRECT) {
      const h = rgbToHsv(rgb);
      setLighting({ hue: h.h, sat: h.s });
      return;
    }
    setBrush(rgb);
    setColour(rgb);
  };

  const saved = Object.entries(keysets).map(([name, indices]) => ({ name, indices }));
  const painted = paintingCustom ? draft.custom : draft.colours;

  return (
    <PageFrame>
      <Stage>
        <StageHead
          title="Lighting"
          subtitle="Select keys, set the colour, then write the frame to the panel."
          right={
            <>
              <span className="micro">Effect</span>
              <span className="value">{fx.name}</span>
              <span className="field__hint">/{fx.code}</span>
            </>
          }
        />
        <StageBody>
          <Keyboard paintable />
        </StageBody>
        <StageFoot>
          <div className="flex items-center gap-3">
            <span className="micro">Workflow</span>
            <span className="field__hint">
              <b style={{ color: "var(--color-dim)", fontWeight: 400 }}>01</b> select keys
            </span>
            <span style={{ color: "var(--color-faint)" }}>→</span>
            <span className="field__hint">
              <b style={{ color: "var(--color-dim)", fontWeight: 400 }}>02</b> set RGB
            </span>
            <span style={{ color: "var(--color-faint)" }}>→</span>
            <span className="field__hint">
              <b style={{ color: "var(--color-dim)", fontWeight: 400 }}>03</b> apply
            </span>
          </div>
          <div className="flex items-center gap-2">
            <span className="field__hint">Brush</span>
            <span
              style={{
                width: 12,
                height: 12,
                borderRadius: 2,
                border: "1px solid rgba(255,255,255,0.18)",
                background: `rgb(${brush[0]}, ${brush[1]}, ${brush[2]})`,
              }}
            />
            <span className="value value--dim">
              {paintingCustom ? "custom" : draft.effect === DIRECT ? "direct" : "paint"}
            </span>
          </div>
        </StageFoot>
      </Stage>

      <Panel foot={<ApplyBar />}>
        <Section no="01" title="Target" aside={target ? `${target} keys` : "all keys"}>
          <div className="chips">
            <button className="chip" onClick={() => void selectAll()}>
              All
            </button>
            <button className="chip" onClick={() => void clearSelection()}>
              None
            </button>
            <button className="chip" onClick={() => void invertSelection()}>
              Invert
            </button>
            {PRESETS.filter((p) => p.name !== "All keys").map((p) => (
              <button
                key={p.name}
                className="chip"
                onClick={() => select(p.indices)}
                title={`${p.indices.length} keys`}
              >
                {p.name}
              </button>
            ))}
          </div>
          <div className="mt-3 flex items-center justify-between">
            <span className="field__hint">
              {target ? "colour goes to the selection" : "colour goes to every key"}
            </span>
            <span className="value">
              {target ? String(target).padStart(2, "0") : "63"}
              <span className="value--dim"> LEDs</span>
            </span>
          </div>
        </Section>

        <Section no="02" title="Mode" aside={`${EFFECTS.length} modes · all real`}>
          <EffectList value={draft.effect} onChange={setEffect} />
          <p className="field__hint" style={{ marginTop: 10, lineHeight: 1.6 }}>
            Painting puts this page in Direct so the frame you build is the frame that runs; Apply
            writes it. Pick <b>Custom</b> to keep a per-key picture in its own flash region, or{" "}
            <b>Solid</b> to drive every key with one colour.
          </p>
        </Section>

        <Section no="03" title="Colour" aside={fx.usesColour ? "base colour" : "per key"}>
          <ColourEditor
            value={current}
            onChange={commitColour}
            disabled={!fx.usesColour && draft.effect !== DIRECT && !paintingCustom}
          />
          {!fx.usesColour && draft.effect !== DIRECT && !paintingCustom && (
            <p className="field__hint" style={{ marginTop: 10, lineHeight: 1.6 }}>
              {fx.name} generates its own colours. Switch to Direct or Custom to paint keys, or use
              the Effects page to tune this one.
            </p>
          )}
        </Section>

        <Section no="04" title="Levels" aside={`val ${draft.val}`}>
          <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
            <span className="field__label">Brightness</span>
            <span className="value">
              {Math.round((draft.val / 254) * 100)}
              <span className="value--dim"> % · {draft.val}</span>
            </span>
          </div>
          <Slider
            min={0}
            max={254}
            step={2}
            value={draft.val}
            onChange={(v) => setLighting({ val: v })}
            label="Brightness"
          />
          <div className="flex items-center gap-2" style={{ marginTop: 2 }}>
            <button className="chip" onClick={() => setLighting({ val: 64 })}>
              Low
            </button>
            <button className="chip" onClick={() => setLighting({ val: 160 })}>
              Mid
            </button>
            <button className="chip" onClick={() => setLighting({ val: 254 })}>
              Max
            </button>
            <button className="chip" onClick={() => setLighting({ val: 0 })}>
              Blackout
            </button>
          </div>

          <div
            className="flex items-center justify-between"
            style={{ marginTop: 14, marginBottom: 4 }}
          >
            <span className="field__label">Speed</span>
            <span className="value">
              {speedStep(draft.speed)}
              <span className="value--dim"> / 8</span>
            </span>
          </div>
          <Slider
            min={1}
            max={8}
            value={speedStep(draft.speed)}
            onChange={(v) => setLighting({ speed: speedByte(v) })}
            disabled={!fx.usesSpeed && !rearAnimates(draft)}
            label="Speed"
          />
          <p className="field__hint" style={{ marginTop: 6 }}>
            {fx.usesSpeed
              ? "Clock advances once per displayed frame (~89 Hz)."
              : rearAnimates(draft)
                ? "This key effect is static, but the rear animation runs on the same clock."
                : "This effect does not animate."}
          </p>
        </Section>

        <Section no="05" title="Rear light" aside={draft.backOn ? "on" : "off"}>
          <div className="flex items-center gap-2">
            <button
              className="chip"
              data-active={draft.backOn === 1}
              onClick={() => setLighting({ backOn: 1 })}
            >
              On
            </button>
            <button
              className="chip"
              data-active={draft.backOn === 0}
              onClick={() => setLighting({ backOn: 0 })}
            >
              Off
            </button>
            <span className="field__hint" style={{ marginLeft: "auto" }}>
              FN + B toggles it on the board
            </span>
          </div>
          <p className="field__hint" style={{ marginTop: 6 }}>
            Rear-light changes go to the board as soon as you make them — no APPLY needed.
          </p>

          <div style={{ marginTop: 12 }}>
            <span className="field__label">Animation</span>
            <div className="chips" style={{ marginTop: 6 }}>
              {BACK_MODES.map((m) => (
                <button
                  key={m.id}
                  className="chip"
                  title={m.note}
                  data-active={draft.backMode === m.id}
                  onClick={() => setLighting({ backMode: m.id })}
                >
                  {m.name}
                </button>
              ))}
            </div>
          </div>

          <div style={{ marginTop: 12 }}>
            <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
              <span className="field__label">Level</span>
              <span className="value">
                {Math.round((draft.backLevel / 127) * 100)}
                <span className="value--dim"> %</span>
              </span>
            </div>
            <Slider
              min={0}
              max={127}
              value={draft.backLevel}
              onChange={(v) => setLighting({ backLevel: v })}
              label="Rear level"
            />
          </div>
          <div className="mt-3 flex items-center gap-3">
            <span className="field__label">Colour</span>
            <div className="chips" style={{ marginTop: 0 }}>
              {[
                ["White", [255, 255, 255]],
                ["Warm", [255, 190, 140]],
                ["Blue", [150, 200, 255]],
                ["Red", [255, 120, 120]],
                ["Green", [150, 255, 180]],
                ["Off", [0, 0, 0]],
              ].map(([name, rgb]) => (
                <button
                  key={name as string}
                  className="chip"
                  onClick={() => setLighting({ backRgb: rgb as [number, number, number] })}
                  title={`Rear colour ${name}`}
                >
                  <span
                    style={{
                      display: "inline-block",
                      width: 9,
                      height: 9,
                      marginRight: 6,
                      borderRadius: 1,
                      background: `rgb(${(rgb as number[]).join(",")})`,
                    }}
                  />
                  {name}
                </button>
              ))}
            </div>
          </div>
          <div className="mt-2">
            <ColourEditor
              value={draft.backRgb}
              onChange={(rgb) => setLighting({ backRgb: rgb })}
            />
          </div>
          <p className="field__hint" style={{ marginTop: 10, lineHeight: 1.6 }}>
            The six rear LEDs are wired to two spare commons and driven together, so they show one
            colour at a time; every animation above moves in time rather than chasing along the
            strip. They keep their own level and stay lit through a blackout.
          </p>
        </Section>

        <Section no="06" title="Keysets" aside={`${saved.length} saved`}>
          {saved.length > 0 && (
            <div className="chips" style={{ marginBottom: 10 }}>
              {saved.map((k) => (
                <button
                  key={k.name}
                  className="chip"
                  onClick={() => select(k.indices)}
                  title={`${k.indices.length} keys — right-click to delete`}
                  onContextMenu={(e) => {
                    e.preventDefault();
                    void deleteKeyset(k.name);
                  }}
                >
                  {k.name}
                </button>
              ))}
            </div>
          )}
          <div className="flex items-center gap-2">
            <input
              className="input"
              placeholder={target ? "Name this selection…" : "Select keys first"}
              value={keysetName}
              disabled={!target}
              onChange={(e) => setKeysetName(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter" && keysetName.trim() && target) {
                  const name = keysetName.trim();
                  void saveKeyset(name, selection).then((ok) => {
                    if (ok) setKeysetName("");
                  });
                }
              }}
            />
            <button
              className="btn btn--sm"
              disabled={!target || !keysetName.trim()}
              onClick={() => {
                const name = keysetName.trim();
                void saveKeyset(name, selection).then((ok) => {
                  if (ok) setKeysetName("");
                });
              }}
            >
              Save
            </button>
          </div>
          <p className="field__hint" style={{ marginTop: 8, lineHeight: 1.6 }}>
            Keysets are saved on the host and reload a selection, here or from the Profiles page.
          </p>
          <div className="mt-3 flex items-center justify-between">
            <span className="field__hint">Painted keys</span>
            <span className="value">
              {painted.slice(0, 63).filter((c) => c[0] + c[1] + c[2] > 0).length}
              <span className="value--dim"> / 63</span>
            </span>
          </div>
          <div className="mt-2 flex items-center gap-2">
            <button
              className="chip"
              data-active={draft.effect === DIRECT}
              onClick={() => setEffect(DIRECT)}
            >
              {paintingCustom ? "Switch to Direct" : "Direct"}
            </button>
            <button
              className="chip"
              data-active={paintingCustom}
              onClick={() => setEffect(34)}
            >
              Custom picture
            </button>
            <button
              className="chip"
              onClick={() => setBrush([255, 245, 235])}
              title="Back to the default warm white"
            >
              Reset brush
            </button>
          </div>
        </Section>
      </Panel>
    </PageFrame>
  );
}
