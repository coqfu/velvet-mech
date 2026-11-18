import { useMemo, useState } from "react";
import { ColourEditor } from "../components/ColourEditor";
import { EffectList } from "../components/EffectList";
import { Keyboard } from "../components/Keyboard";
import { ApplyBar, PageFrame, Panel, Stage, StageBody, StageFoot } from "../components/PageFrame";
import { KV, Section, Slider, StageHead } from "../components/ui";
import { hsvToRgb, rgbToHsv } from "../lib/colour";
import { EFFECTS, effectById, rearAnimates, speedByte, speedStep, type EffectGroup } from "../lib/effects";
import { DIRECT } from "../lib/types";
import { useStore } from "../state/store";

const GROUPS: { id: EffectGroup | "all"; label: string }[] = [
  { id: "all", label: "All" },
  { id: "basic", label: "Basic" },
  { id: "spectrum", label: "Spectrum" },
  { id: "pattern", label: "Pattern" },
  { id: "texture", label: "Texture" },
  { id: "reactive", label: "Reactive" },
];

export function EffectsPage() {
  const { draft, setEffect, setLighting } = useStore();
  const [group, setGroup] = useState<EffectGroup | "all">("all");
  const fx = effectById(draft.effect);
  const base = hsvToRgb(draft.hue, draft.sat, 255);

  const visible = useMemo(
    () => (group === "all" ? EFFECTS : EFFECTS.filter((e) => e.group === group)),
    [group]
  );

  return (
    <PageFrame>
      <Stage>
        <StageHead
          title="Effects"
          subtitle="Every effect runs on the keyboard itself - the host only sets the parameters."
          right={
            <>
              <span className="micro">Module</span>
              <span className="value">
                {fx.code} · {fx.name}
              </span>
            </>
          }
        />
        <StageBody>
          <Keyboard />
        </StageBody>
        <StageFoot>
          <p className="field__hint field__hint--prose" style={{ maxWidth: 620, lineHeight: 1.6, whiteSpace: "normal" }}>
            Thirty-five firmware effects plus Direct. The preview on screen is generated with the
            same arithmetic as the panel, including the 6-bit dimmer and the white balance, so it
            follows brightness, speed and calibration the way the panel does.
          </p>
          <p className="field__hint field__hint--prose" style={{ maxWidth: 620, lineHeight: 1.6, whiteSpace: "normal", marginTop: 8 }}>
            Fn + [ ] \ step the effect, and Fn with the arrows sets brightness and speed on the
            board itself. Those changes take effect immediately but are not written to the flash,
            because the write stops the LED scan for about 160 ms and the panel blinks. Save to
            flash is what comes back after a replug.
          </p>
          <span className="micro">{EFFECTS.length} modes</span>
        </StageFoot>
      </Stage>

      <Panel
        foot={
          <ApplyBar
            note={
              fx.usesSpeed
                ? "speed applies"
                : rearAnimates(draft)
                  ? "rear clock applies"
                  : "static"
            }
          />
        }
      >
        <Section no="01" title="Library" aside={`${visible.length} of ${EFFECTS.length}`}>
          <div className="chips" style={{ marginBottom: 10 }}>
            {GROUPS.map((g) => (
              <button
                key={g.id}
                className="chip"
                data-active={group === g.id}
                onClick={() => setGroup(g.id)}
              >
                {g.label}
              </button>
            ))}
          </div>
          <EffectList value={draft.effect} onChange={setEffect} group={group} />
        </Section>

        <Section no="02" title={fx.name} aside={fx.code}>
          <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6, marginBottom: 12 }}>
            {fx.desc}
          </p>
          <KV
            rows={[
              ["Colour", draft.effect === DIRECT ? "painted per key" : fx.usesColour ? "base hue + saturation" : "effect-owned"],
              ["Motion", fx.usesSpeed ? "clocked, 1–8×" : "static"],
              ["Input", draft.effect === DIRECT ? "host frame" : fx.reactive ? "key presses" : "none"],
              ["Direction", "fixed in firmware"],
            ]}
          />
        </Section>

        {draft.effect === DIRECT && (
          <Section no="03" title="Direct frame" aside="host-painted">
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              Direct is not a generated effect: each LED takes the colour in the per-key frame
              instead of a hue and saturation. Paint that frame on the Lighting page. The brightness
              slider below still acts as the master dimmer for the frame.
            </p>
          </Section>
        )}

        {fx.usesColour && draft.effect !== DIRECT && (
          <Section no="03" title="Base colour" aside="hue / saturation">
            <ColourEditor
              value={base}
              onChange={(rgb) => {
                const h = rgbToHsv(rgb);
                setLighting({ hue: h.h, sat: h.s });
              }}
            />
            <p className="field__hint field__hint--prose" style={{ marginTop: 10, lineHeight: 1.6 }}>
              Value is the master brightness below; the effect scales this colour itself.
            </p>
          </Section>
        )}

        <Section no="04" title="Levels" aside={`val ${draft.val}`}>
          <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
            <span className="field__label">Brightness</span>
            <span className="value">
              {Math.round((draft.val / 254) * 100)}
              <span className="value--dim"> %</span>
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
              ? "The clock advances once per published frame, so every speed divides cleanly."
              : rearAnimates(draft)
                ? "This key effect is static, but the rear animation runs on the same clock."
                : "This effect has no clock; the slider is disabled rather than ignored."}
          </p>
        </Section>

        {fx.custom && (
          <Section no="05" title="Custom picture" aside="stored in the board">
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              Custom uses the 63-value picture instead of a generated pattern. Open the Lighting
              page, select keys with the Paint tool, and every colour you brush lands in this
              picture. It lives in its own flash block, so it survives a power cycle.
            </p>
            <div className="mt-3">
              <KV
                rows={[
                  ["Storage", "data flash 0xF0C0 · 3 blocks"],
                  ["Values", "63 keys × RGB"],
                  ["Edited on", "Lighting → Paint"],
                  ["Saved by", "Apply, or Save to flash"],
                ]}
              />
            </div>
          </Section>
        )}

        {fx.reactive && (
          <Section no="05" title="Reactive fade" aside="per key">
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              The board keeps a fade value per key. A press sets it to full and it decays every
              frame, so chords build up and then cool down. Click keys on the board to preview the
              same behaviour here.
            </p>
            <div className="mt-3">
              <KV
                rows={[
                  ["Tracker", "led_fade[16 × 5], one byte per key"],
                  ["On press", "fade = 255"],
                  ["Decay", "once per published frame"],
                  ["Base", "effect-specific dimmed colour"],
                ]}
              />
            </div>
          </Section>
        )}
      </Panel>
    </PageFrame>
  );
}
