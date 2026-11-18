import { useEffect, useRef, useState } from "react";
import {
  PALETTE,
  clamp,
  hexToRgb,
  hsvToRgb,
  rgbToHex,
  rgbToHsv,
} from "../lib/colour";
import type { Rgb } from "../lib/types";
import { NumField, Slider } from "./ui";

/**
 * RGB + HSV editor.  HSV is kept locally so a black colour can still be
 * navigated (at v=0 every hue is black, which would otherwise pin the hue
 * slider in place).
 */
export function ColourEditor({
  value,
  onChange,
  disabled,
}: {
  value: Rgb;
  onChange: (rgb: Rgb) => void;
  disabled?: boolean;
}) {
  const [hsv, setHsv] = useState(() => rgbToHsv(value));
  const [hex, setHex] = useState(() => rgbToHex(value));
  const selfEdit = useRef(false);

  useEffect(() => {
    if (selfEdit.current) {
      selfEdit.current = false;
      return;
    }
    setHsv(rgbToHsv(value));
    setHex(rgbToHex(value));
  }, [value]);

  const emitHsv = (h: number, s: number, v: number) => {
    setHsv({ h, s, v });
    const rgb = hsvToRgb(h, s, v);
    setHex(rgbToHex(rgb));
    selfEdit.current = true;
    onChange(rgb);
  };

  const emitRgb = (rgb: Rgb) => {
    setHex(rgbToHex(rgb));
    setHsv(rgbToHsv(rgb));
    selfEdit.current = true;
    onChange(rgb);
  };

  const hueCss = `hsl(${Math.round((hsv.h / 255) * 360)}deg 100% 50%)`;
  const satBg = `linear-gradient(90deg, hsl(${Math.round((hsv.h / 255) * 360)}deg 0% ${Math.round(
    (hsv.v / 255) * 50
  )}%), hsl(${Math.round((hsv.h / 255) * 360)}deg 100% ${Math.round((hsv.v / 255) * 50)}%))`;
  const valBg = `linear-gradient(90deg, #000, hsl(${Math.round(
    (hsv.h / 255) * 360
  )}deg ${Math.round((hsv.s / 255) * 100)}% 50%))`;

  return (
    <div style={{ opacity: disabled ? 0.42 : 1, pointerEvents: disabled ? "none" : undefined }}>
      <div className="colour-readout">
        <div
          className="colour-chip"
          style={{ background: `rgb(${value[0]}, ${value[1]}, ${value[2]})` }}
        />
        <div style={{ flex: 1 }}>
          <div className="micro" style={{ marginBottom: 5 }}>
            Hex
          </div>
          <input
            className="input input--hex"
            value={hex}
            spellCheck={false}
            onChange={(e) => {
              setHex(e.target.value);
              const rgb = hexToRgb(e.target.value);
              if (rgb) emitRgb(rgb);
            }}
            onBlur={() => setHex(rgbToHex(value))}
          />
        </div>
      </div>

      <div className="mt-3 flex items-center gap-2">
        {(["R", "G", "B"] as const).map((ch, i) => (
          <div key={ch} className="flex items-center gap-1" style={{ flex: 1 }}>
            <span className="micro" style={{ color: "var(--color-faint)" }}>
              {ch}
            </span>
            <NumField
              value={value[i]}
              width={44}
              onChange={(n) => {
                const next = [...value] as Rgb;
                next[i] = n;
                emitRgb(next);
              }}
            />
          </div>
        ))}
      </div>

      <div className="mt-3">
        <div className="flex items-center justify-between" style={{ marginBottom: 2 }}>
          <span className="micro">Hue</span>
          <span className="value value--dim">{Math.round((hsv.h / 255) * 360)}°</span>
        </div>
        <Slider
          min={0}
          max={255}
          value={hsv.h}
          variant="hue"
          onChange={(h) => emitHsv(h, hsv.s, hsv.v)}
          label="Hue"
        />
        <div className="flex items-center justify-between" style={{ marginBottom: 2, marginTop: 6 }}>
          <span className="micro">Saturation</span>
          <span className="value value--dim">
            {Math.round((hsv.s / 255) * 100)}%
            <span style={{ color: "var(--color-faint)" }}> · {hsv.s}</span>
          </span>
        </div>
        <input
          className="slider slider--s"
          type="range"
          min={0}
          max={255}
          value={hsv.s}
          aria-label="Saturation"
          onChange={(e) => emitHsv(hsv.h, Number(e.target.value), hsv.v)}
          style={{ "--sbg": satBg } as React.CSSProperties}
        />
        <div className="flex items-center justify-between" style={{ marginBottom: 2, marginTop: 6 }}>
          <span className="micro">Value</span>
          <span className="value value--dim">
            {Math.round((hsv.v / 255) * 100)}%
            <span style={{ color: "var(--color-faint)" }}> · {hsv.v}</span>
          </span>
        </div>
        <input
          className="slider slider--v"
          type="range"
          min={0}
          max={255}
          value={hsv.v}
          aria-label="Value"
          onChange={(e) => emitHsv(hsv.h, hsv.s, Number(e.target.value))}
          style={{ "--sbg": valBg } as React.CSSProperties}
        />
      </div>

      <div className="swatches mt-4">
        {PALETTE.map((p) => (
          <button
            key={p.name}
            type="button"
            className="swatch"
            title={p.name}
            aria-label={p.name}
            data-active={p.rgb.every((c, i) => Math.abs(c - value[i]) < 3)}
            style={{
              background: `rgb(${p.rgb[0]}, ${p.rgb[1]}, ${p.rgb[2]})`,
            }}
            onClick={() => emitRgb(p.rgb)}
          />
        ))}
      </div>

      <div
        className="mt-2 flex items-center gap-2"
        style={{ color: "var(--color-faint)", borderTop: "1px solid var(--color-line)", paddingTop: 8 }}
      >
        <span className="field__hint">Hue {Math.round((hsv.h / 255) * 360)}°</span>
        <span style={{ flex: 1, height: 1, background: "var(--color-line)" }} />
        <span className="field__hint">{clamp(value[0], 0, 255)}·{value[1]}·{value[2]}</span>
      </div>
    </div>
  );
}
