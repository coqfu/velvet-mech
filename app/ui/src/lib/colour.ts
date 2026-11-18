import type { Rgb } from "./types";

export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

export function rgbToHex([r, g, b]: Rgb): string {
  const h = (n: number) => clamp(Math.round(n), 0, 255).toString(16).padStart(2, "0");
  return `#${h(r)}${h(g)}${h(b)}`.toUpperCase();
}

export function hexToRgb(hex: string): Rgb | null {
  let s = hex.trim().replace(/^#/, "");
  if (s.length === 3) s = s.split("").map((c) => c + c).join("");
  if (!/^[0-9a-f]{6}$/i.test(s)) return null;
  return [
    parseInt(s.slice(0, 2), 16),
    parseInt(s.slice(2, 4), 16),
    parseInt(s.slice(4, 6), 16),
  ];
}

/** 0..255 rgb -> h 0..255, s 0..255, v 0..255 (matches the firmware's space). */
export function rgbToHsv([r, g, b]: Rgb): { h: number; s: number; v: number } {
  const rn = r / 255;
  const gn = g / 255;
  const bn = b / 255;
  const max = Math.max(rn, gn, bn);
  const min = Math.min(rn, gn, bn);
  const d = max - min;
  let h = 0;
  if (d !== 0) {
    if (max === rn) h = ((gn - bn) / d) % 6;
    else if (max === gn) h = (bn - rn) / d + 2;
    else h = (rn - gn) / d + 4;
    h /= 6;
    if (h < 0) h += 1;
  }
  return {
    h: Math.round(h * 255) & 255,
    s: Math.round((max === 0 ? 0 : d / max) * 255),
    v: Math.round(max * 255),
  };
}

export function hsvToRgb(h: number, s: number, v: number): Rgb {
  const hn = (h & 255) / 255;
  const sn = clamp(s, 0, 255) / 255;
  const vn = clamp(v, 0, 255) / 255;
  const i = Math.floor(hn * 6) % 6;
  const f = hn * 6 - Math.floor(hn * 6);
  const p = vn * (1 - sn);
  const q = vn * (1 - f * sn);
  const t = vn * (1 - (1 - f) * sn);
  let r = 0;
  let g = 0;
  let b = 0;
  if (i === 0) [r, g, b] = [vn, t, p];
  else if (i === 1) [r, g, b] = [q, vn, p];
  else if (i === 2) [r, g, b] = [p, vn, t];
  else if (i === 3) [r, g, b] = [p, q, vn];
  else if (i === 4) [r, g, b] = [t, p, vn];
  else [r, g, b] = [vn, p, q];
  return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
}

export const scale = ([r, g, b]: Rgb, k: number): Rgb => [
  Math.round(r * k),
  Math.round(g * k),
  Math.round(b * k),
];

/** Perceptual brightness 0..1, used to decide legend contrast and glow alpha. */
export function luminance([r, g, b]: Rgb): number {
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
}

/** How strongly the keycap should glow for a given rendered colour. */
export function glowAlpha(rgb: Rgb): number {
  const l = luminance(rgb);
  if (l <= 0.004) return 0;
  return clamp(0.1 + l * 0.85, 0, 0.94);
}

/** Legend colour for a lit key: the LED colour lifted towards white. */
export function legendTint(rgb: Rgb): string {
  const l = luminance(rgb);
  const k = 0.45 + l * 0.35;
  const mix = (c: number) => Math.round(c * (1 - k) + 255 * k);
  return `rgb(${mix(rgb[0])}, ${mix(rgb[1])}, ${mix(rgb[2])})`;
}

export const PALETTE: { name: string; rgb: Rgb }[] = [
  { name: "White", rgb: [255, 255, 255] },
  { name: "Warm", rgb: [255, 176, 120] },
  { name: "Amber", rgb: [255, 140, 40] },
  { name: "Red", rgb: [255, 40, 48] },
  { name: "Magenta", rgb: [255, 40, 200] },
  { name: "Violet", rgb: [140, 80, 255] },
  { name: "Blue", rgb: [40, 110, 255] },
  { name: "Cyan", rgb: [40, 230, 255] },
  { name: "Teal", rgb: [0, 200, 190] },
  { name: "Green", rgb: [40, 255, 120] },
  { name: "Lime", rgb: [170, 255, 60] },
  { name: "Ice", rgb: [170, 210, 255] },
  { name: "Rose", rgb: [255, 120, 150] },
  { name: "Sand", rgb: [220, 200, 170] },
  { name: "Slate", rgb: [90, 100, 120] },
  { name: "Off", rgb: [0, 0, 0] },
];
