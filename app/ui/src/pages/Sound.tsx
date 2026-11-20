import { useCallback, useEffect, useMemo, useState } from "react";
import { PageFrame, Panel, Stage, StageBody, StageFoot } from "../components/PageFrame";
import { Chips, KV, Metric, Section, Slider, StageHead } from "../components/ui";
import { api } from "../lib/api";
import type { AudioState } from "../lib/types";
import { useStore } from "../state/store";

const MODE_LABEL: Record<string, string> = {
  spectrum: "Spectrum",
  pulse: "Pulse",
  wave: "Wave",
  meter: "Meter",
  spark: "Spark",
  ripple: "Ripple",
  comet: "Comet",
};

const PALETTE_LABEL: Record<string, string> = {
  prism: "Prism",
  mono: "Mono",
  heat: "Heat",
  ocean: "Ocean",
  violet: "Violet",
  sunset: "Sunset",
};

/** Rolling band display.  Fourteen bars is not a decoration: it is the
 *  exact band count the analyser produces. */
function Bands({ bands }: { bands: number[] }) {
  return (
    <div className="sound">
      <div className="sound-bars">
        {bands.map((v, i) => (
          <div className="sound-bar" key={i} title={`band ${i + 1}: ${Math.round(v * 100)}%`}>
            <div className="sound-bar__fill" style={{ height: `${Math.max(1, v * 100)}%` }} />
          </div>
        ))}
      </div>
      <div className="sound-bars__foot">
        <span>45 hz</span>
        <span>14 bands · log</span>
        <span>16 khz</span>
      </div>
    </div>
  );
}

export function SoundPage() {
  const { connected, refresh, toast } = useStore();
  const [audio, setAudio] = useState<AudioState | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const poll = useCallback(async () => {
    try {
      const next = await api.audio();
      setAudio(next);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : "the service did not answer");
    }
  }, []);

  useEffect(() => {
    void poll();
    const id = window.setInterval(() => void poll(), 250);
    return () => window.clearInterval(id);
  }, [poll]);

  const send = useCallback(
    async (patch: Parameters<typeof api.setAudio>[0], announce?: string) => {
      setBusy(true);
      setError(null);
      try {
        const next = await api.setAudio(patch);
        setAudio(next);
        if (announce) toast(announce, "ok");
        void refresh();
      } catch (err) {
        setError(err instanceof Error ? err.message : "the request failed");
      } finally {
        setBusy(false);
      }
    },
    [refresh, toast]
  );

  const params = audio?.params;
  const running = audio?.running ?? false;
  const level = audio?.level ?? 0;
  const bands = useMemo(
    () => (audio && audio.bands.length === 14 ? audio.bands : Array(14).fill(0)),
    [audio]
  );

  const mode = params?.mode ?? "spectrum";
  const palette = params?.palette ?? "prism";
  const gain = params?.gain ?? 50;
  const ceiling = params?.ceiling ?? 200;
  const source = audio?.source ?? null;
  const problem = error ?? audio?.error ?? null;

  return (
    <PageFrame>
      <Stage>
        <StageHead
          title="Sound"
          subtitle="Reactive lighting analysed from this machine's audio output. The board has no microphone - the frames are rendered here and streamed over the LED path."
          right={
            <>
              <span className="dot" data-state={running ? "ok" : "warn"} />
              <span className="micro">{running ? "listening" : "stopped"}</span>
            </>
          }
        />
        <StageBody top>
          <div className="metric-grid">
            <Metric label="Level" value={audio ? (level * 100).toFixed(0) : "—"} unit=" %" />
            <Metric label="Capture" value={running ? `${audio?.fps.toFixed(0) ?? "—"}` : "—"} unit=" fps" />
            <Metric label="Bands" value={audio ? bands.filter((v) => v > 0.02).length : "—"} unit=" / 14" />
            <Metric label="Ceiling" value={ceiling} unit=" / 255" />
          </div>

          <div className="sound-level" aria-hidden="true">
            <div className="sound-level__fill" style={{ width: `${Math.max(0, Math.min(100, level * 100))}%` }} />
          </div>

          <Bands bands={bands} />

          <div style={{ marginTop: 14 }}>
            <KV
              rows={[
                ["Input", source ?? "no capture source"],
                ["Capture state", running ? "running" : "idle"],
                ["Stream", running ? `${audio?.fps.toFixed(1) ?? "—"} fps of ${audio?.targetFps ?? 45}` : "not streaming"],
                ["Rendered by", "host (app/server.py) - not firmware"],
              ]}
            />
          </div>

          {problem && (
            <p className="field__hint field__hint--prose" style={{ marginTop: 12, color: "var(--color-warn)" }}>
              {problem}
            </p>
          )}
          {!problem && !running && audio?.note && (
            <p className="field__hint" style={{ marginTop: 12 }}>
              {audio.note}
            </p>
          )}
        </StageBody>
        <StageFoot>
          <span className="field__hint">
            {running
              ? "Leave the page and it keeps running; Stop returns the board to its previous effect."
              : "Start listening to switch the board to Direct and stream frames."}
          </span>
          <span className="micro">{connected ? "board connected" : "board offline"}</span>
        </StageFoot>
      </Stage>

      <Panel>
        <Section no="01" title="Output" aside={running ? "live" : "off"}>
          <button
            className={`btn ${running ? "btn--quiet" : "btn--primary"}`}
            style={{ width: "100%" }}
            disabled={busy || (!running && (!connected || !source))}
            onClick={() => void send({ enabled: !running }, running ? "sound mode stopped" : "sound mode started")}
          >
            {busy ? "Writing…" : running ? "Stop listening" : "Start listening"}
          </button>
          <p className="field__hint" style={{ marginTop: 8, lineHeight: 1.6 }}>
            {source
              ? `Recording ${source}.`
              : "No capture source was found. Play something through an output device: Windows records it as WASAPI loopback, Linux exposes it as a monitor."}
          </p>
          <p className="field__hint field__hint--prose" style={{ marginTop: 8, lineHeight: 1.6 }}>
            The keyboard has no microphone and no audio input. This page records what the machine is
            playing - the output monitor on Linux, WASAPI loopback on Windows - analyses it here, and
            sends one colour per LED. Nothing about the sound is stored on the board.
          </p>
        </Section>

        <Section no="02" title="Mode" aside={MODE_LABEL[mode] ?? mode}>
          <div className="fx-list">
            {(audio?.modes ?? []).map((m, i) => (
              <button
                key={m.id}
                type="button"
                className="fx-item"
                data-active={mode === m.id}
                onClick={() => void send({ mode: m.id })}
                title={m.desc}
              >
                <span className="fx-item__no">{String(i + 1).padStart(2, "0")}</span>
                <span style={{ flex: 1, minWidth: 0, textAlign: "left" }}>
                  <div className="fx-item__name">{MODE_LABEL[m.id] ?? m.id}</div>
                  <div className="fx-item__desc">{m.desc}</div>
                </span>
              </button>
            ))}
          </div>
        </Section>

        <Section no="03" title="Palette" aside={PALETTE_LABEL[palette] ?? palette}>
          <Chips
            items={(audio?.palettes ?? []).map((p) => ({
              id: p.id,
              label: PALETTE_LABEL[p.id] ?? p.id,
            }))}
            active={palette}
            onPick={(id) => void send({ palette: id })}
          />
        </Section>

        <Section no="04" title="Response" aside={`${gain} / ${ceiling}`}>
          <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
            <span className="field__label">Gain</span>
            <span className="value">
              {gain}
              <span className="value--dim"> / 100</span>
            </span>
          </div>
          <Slider min={0} max={100} value={gain} onChange={(v) => void send({ gain: v })} label="Gain" />
          <p className="field__hint" style={{ marginTop: 4, marginBottom: 12 }}>
            Raises quiet material into range. 50 is a neutral start.
          </p>

          <div className="flex items-center justify-between" style={{ marginBottom: 4 }}>
            <span className="field__label">Ceiling</span>
            <span className="value">
              {ceiling}
              <span className="value--dim"> / 255</span>
            </span>
          </div>
          <Slider
            min={20}
            max={255}
            value={ceiling}
            onChange={(v) => void send({ ceiling: v })}
            label="Brightness ceiling"
          />
          <p className="field__hint" style={{ marginTop: 4 }}>
            Caps the brightness of the key frame without touching the board's saved brightness. The
            rear strip is not scaled by it; it keeps the level set on the Lighting page.
          </p>
        </Section>

        <Section no="05" title="Capture source" aside={audio ? `${audio.sources.length} sources` : "—"}>
          <Chips
            items={(audio?.sources ?? []).map((s) => ({
              id: s.id,
              label: s.id.replace(/\.monitor$/, "").slice(-22),
            }))}
            active={source}
            onPick={(id) => void send({ source: id, ...(running ? { enabled: true } : {}) })}
          />
          <p className="field__hint field__hint--prose" style={{ marginTop: 8, lineHeight: 1.6 }}>
            On Linux a monitor is the sound server's copy of what a sink is playing; on Windows the
            same thing is the WASAPI loopback of the default speaker. Choose the output you actually
            hear - a Bluetooth sink may expose its monitor only while it is playing.
          </p>
        </Section>
      </Panel>
    </PageFrame>
  );
}
