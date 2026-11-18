
import { KEYS, LED_COUNT } from "./keyboard-data";
import type { Lighting, Rgb } from "./types";

import { DIRECT } from "./types";

export type EffectGroup = "basic" | "spectrum" | "pattern" | "texture" | "reactive";

export type EffectDef = {
  id: number;
  code: string;
  name: string;
  desc: string;
  group: EffectGroup;
  /** Uses the base colour (hue/sat). */
  usesColour: boolean;
  /** Advances with the animation clock. */
  usesSpeed: boolean;
  /** Lights keys from the per-key fade table. */
  reactive: boolean;
  /** Draws the per-key Custom picture. */
  custom: boolean;
};

/**
 * Every effect the firmware can render, in `LED_FX_*` order.  This table is
 * the single source of truth for the UI: an entry exists only if `pixel()` in
 * src/led.c has a matching case, so nothing here can be a dead option.
 */
export const EFFECTS: EffectDef[] = [
  {
    id: DIRECT,
    code: "00",
    name: "Direct",
    desc: "Per-key colour, one value per LED. Painted over the LED path.",
    group: "basic",
    usesColour: false,
    usesSpeed: false,
    reactive: false,
    custom: false,
  },
  {
    id: 0,
    code: "01",
    name: "Off",
    desc: "Panel dark. The rear light keeps its own switch.",
    group: "basic",
    usesColour: false,
    usesSpeed: false,
    reactive: false,
    custom: false,
  },
  {
    id: 1,
    code: "02",
    name: "Solid",
    desc: "One colour across the whole board.",
    group: "basic",
    usesColour: true,
    usesSpeed: false,
    reactive: false,
    custom: false,
  },
  {
    id: 2,
    code: "03",
    name: "Breathing",
    desc: "Base colour rises and falls.",
    group: "basic",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 3,
    code: "04",
    name: "Rainbow",
    desc: "Hue bands marching across the columns.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 4,
    code: "05",
    name: "Aurora",
    desc: "Teal at the top, violet below, slow diagonal shimmer.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 5,
    code: "06",
    name: "Wave",
    desc: "A cyan crest sweeping diagonally over a dim base.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 6,
    code: "07",
    name: "Reactive",
    desc: "Held keys flash warm and fade back to deep blue.",
    group: "reactive",
    usesColour: false,
    usesSpeed: false,
    reactive: true,
    custom: false,
  },
  {
    id: 7,
    code: "08",
    name: "Spectrum",
    desc: "Hue bands stepping down the rows.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 8,
    code: "09",
    name: "Rain Wave",
    desc: "Diagonal rainbow drifting across the board.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 9,
    code: "10",
    name: "Colour Wave",
    desc: "One hue rolling through a wave shape.",
    group: "spectrum",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 10,
    code: "11",
    name: "Gradient",
    desc: "Bright at the top, dim at the bottom, slow shimmer.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 11,
    code: "12",
    name: "Spiral",
    desc: "Hue swirl with a breathing brightness.",
    group: "pattern",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 12,
    code: "13",
    name: "Checker",
    desc: "Two-state checkerboard toggling over time.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 13,
    code: "14",
    name: "Strobe",
    desc: "Whole board flashing on a fixed cadence.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 14,
    code: "15",
    name: "Alternate",
    desc: "Rows alternate between lit and dim.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 15,
    code: "16",
    name: "Twinkle",
    desc: "Scattered white sparkles over a dim base.",
    group: "texture",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 16,
    code: "17",
    name: "Starlight",
    desc: "Sparse stars on a black sky.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 17,
    code: "18",
    name: "Fire",
    desc: "Red and orange flicker, hotter towards the bottom.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 18,
    code: "19",
    name: "Ocean",
    desc: "Blue swell rolling through the keys.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 19,
    code: "20",
    name: "Sunset",
    desc: "Static warm gradient, one hue per row.",
    group: "texture",
    usesColour: false,
    usesSpeed: false,
    reactive: false,
    custom: false,
  },
  {
    id: 20,
    code: "21",
    name: "Vapor",
    desc: "Pastel hues drifting diagonally.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 21,
    code: "22",
    name: "Matrix",
    desc: "Green columns of code drifting through the board.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 22,
    code: "23",
    name: "Snow",
    desc: "Cool white flecks drifting through blue.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 23,
    code: "24",
    name: "Ripple",
    desc: "Blue rings expanding from the centre.",
    group: "pattern",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 24,
    code: "25",
    name: "Breathing Rain",
    desc: "Rainbow bands under a whole-board breath.",
    group: "spectrum",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 25,
    code: "26",
    name: "Heartbeat",
    desc: "Double pulse in red, like a pulse oximeter.",
    group: "pattern",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 26,
    code: "27",
    name: "Scanner",
    desc: "A single column sweeping across the board.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 27,
    code: "28",
    name: "Laser",
    desc: "A row sweeping across the board.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 28,
    code: "29",
    name: "Pulse",
    desc: "Rings stepping outwards from the centre.",
    group: "pattern",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 29,
    code: "30",
    name: "Cycle Row",
    desc: "One row lit at a time, walking down the board.",
    group: "pattern",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 30,
    code: "31",
    name: "Random Fade",
    desc: "Uncorrelated per-key fades, no two keys in step.",
    group: "texture",
    usesColour: true,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 31,
    code: "32",
    name: "Heatmap",
    desc: "Keys heat up while held and cool down after.",
    group: "reactive",
    usesColour: false,
    usesSpeed: false,
    reactive: true,
    custom: false,
  },
  {
    id: 32,
    code: "33",
    name: "Rain Reactive",
    desc: "Rainbow rain that brightens where you type.",
    group: "reactive",
    usesColour: false,
    usesSpeed: true,
    reactive: true,
    custom: false,
  },
  {
    id: 33,
    code: "34",
    name: "Candy",
    desc: "Striped pastel hue drift with alternating cells.",
    group: "texture",
    usesColour: false,
    usesSpeed: true,
    reactive: false,
    custom: false,
  },
  {
    id: 34,
    code: "35",
    name: "Custom",
    desc: "Per-key picture painted here and stored in the board.",
    group: "basic",
    usesColour: false,
    usesSpeed: false,
    reactive: false,
    custom: true,
  },
];

export const effectById = (id: number): EffectDef =>
  EFFECTS.find((e) => e.id === id) ?? EFFECTS[0];

/** Raw speed byte -> the firmware's 1..8 multiplier. */
export const speedStep = (speed: number) => 1 + (speed >> 5);

/** 1..8 -> a raw byte that maps back onto the same step. */
export const speedByte = (step: number) => Math.min(7, Math.max(0, step - 1)) << 5;

/* --------------------------------------------------------------- firmware
 * The helpers below are literal ports of the macros in src/led.c.  They have
 * to stay bit-for-bit identical: the preview is only useful if a colour that
 * looks right on screen is the colour the board emits, quirks included.
 */

const u8 = (x: number) => x & 0xff;

/** 0..255 triangle over 512 steps, exactly periodic. */
const tri16 = (x: number) => {
  const v = x & 0x1ff;
  return (v & 0x100) ? 255 - (v & 0xff) : v & 0xff;
};

const scale8 = (v: number, s: number) => u8((v * s) >> 8);

/** Cheap 8-bit hash used by the sparkle and random effects. */
const mix8 = (a: number) => {
  let x = a & 0xff;
  x ^= u8(x << 3);
  x = u8(x + u8(a >> 8) + 0x37);
  x ^= u8(x >> 5);
  return u8(x * 29);
};

/** Hue in quarter units (0..1023) -> rgb, same sectors as hue2rgb(). */
function hue2rgb(hueQ2: number): Rgb {
  const x = (hueQ2 + (hueQ2 >> 1)) & 0xffff;
  const s = (x >> 8) & 0xff;
  const f = x & 0xff;
  const up = u8((f * 252) >> 8);
  const dn = u8(252 - up);

  switch (s) {
    case 0:
      return [252, up, 6];
    case 1:
      return [dn, 252, 6];
    case 2:
      return [6, 252, up];
    case 3:
      return [6, dn, 252];
    case 4:
      return [up, 6, 252];
    default:
      return [252, 6, dn];
  }
}

const centreDist = (col: number, row: number) =>
  (col > 6 ? col - 6 : 6 - col) + (row > 2 ? row - 2 : 2 - row);

/** HSV 0..255 -> RGB at full value, a literal port of hsv2rgb(). */
function hsv2rgb(h: number, s: number): Rgb {
  if (s === 0) return [255, 255, 255];
  const region = Math.floor(h / 43);
  const rem = u8((h - region * 43) * 6);
  const p = scale8(255, u8(255 - s));
  const q = u8(255 - ((s * rem) >> 8));
  const t = u8(255 - ((s * (255 - rem)) >> 8));
  switch (region) {
    case 0:
      return [255, t, p];
    case 1:
      return [q, 255, p];
    case 2:
      return [p, 255, t];
    case 3:
      return [p, q, 255];
    case 4:
      return [t, p, 255];
    default:
      return [255, p, q];
  }
}

/** One LED of one frame, mirroring `pixel()` in src/led.c. */
function pixelAt(
  lighting: Lighting,
  col: number,
  row: number,
  t: number,
  fade: number
): { i: number; rgb: Rgb } {
  const base = hsv2rgb(lighting.hue, lighting.sat);
  let r = 0;
  let g = 0;
  let b = 0;
  let inten = lighting.val >> 1; /* led_brightness, 0..127 */
  let v = 0;
  let k = 0;
  let h = 0;
  let d = 0;
  let p = 0;

  switch (lighting.effect) {
    case 0:
      inten = 0;
      break;

    case 1:
      [r, g, b] = base;
      break;

    case 2:
      v = tri16(t >> 1);
      r = scale8(base[0], v);
      g = scale8(base[1], v);
      b = scale8(base[2], v);
      break;

    case 3:
      [r, g, b] = hue2rgb((col * 72 + t) & 1023);
      break;

    case 4: {
      k = 96 + (tri16((t >> 1) + col * 12 + row * 20) >> 1);
      const c = hue2rgb((128 + row * 22) << 2);
      r = scale8(c[0], k);
      g = scale8(c[1], k);
      b = scale8(c[2], k);
      break;
    }

    case 5: {
      d = u8((((col << 6) + (row << 5) - t) & 0xffff) >> 2);
      if (d < 96) v = u8(255 - (d << 1));
      else if (d < 128) v = u8(63 - (((d - 96) * 23) >> 5));
      else v = u8(40 + (((d - 128) * 215) >> 7));
      r = scale8(0, v);
      g = scale8(200, v);
      b = scale8(255, v);
      break;
    }

    case 6:
      if (fade) {
        r = 255;
        g = 235;
        b = 210;
      } else {
        r = 0;
        g = 40;
        b = 110;
        inten = inten >> 2;
      }
      break;

    case 7:
      [r, g, b] = hue2rgb((row * 72 + ((t << 1) & 0xffff)) & 1023);
      break;

    case 8:
      [r, g, b] = hue2rgb((col * 40 + row * 56 + t) & 1023);
      break;

    case 9:
      v = tri16(col * 24 + row * 16 - t);
      r = scale8(base[0], v);
      g = scale8(base[1], v);
      b = scale8(base[2], v);
      break;

    case 10:
      k = u8(200 - row * 34 + (tri16(t >> 2) >> 4));
      r = scale8(base[0], k);
      g = scale8(base[1], k);
      b = scale8(base[2], k);
      break;

    case 11: {
      v = u8(120 + (tri16(t + col * 16) >> 1));
      const c = hue2rgb((col * 48 + row * 96 + ((t << 1) & 0xffff)) & 1023);
      r = scale8(c[0], v);
      g = scale8(c[1], v);
      b = scale8(c[2], v);
      break;
    }

    case 12:
      if (((col + row) & 1) ^ ((t >> 7) & 1)) {
        [r, g, b] = base;
      } else {
        r = scale8(base[0], 40);
        g = scale8(base[1], 40);
        b = scale8(base[2], 40);
      }
      break;

    case 13:
      if ((t >> 6) & 1) [r, g, b] = base;
      else inten = 0;
      break;

    case 14:
      if ((row & 1) ^ ((t >> 7) & 1)) {
        [r, g, b] = base;
      } else {
        r = scale8(base[0], 32);
        g = scale8(base[1], 32);
        b = scale8(base[2], 32);
      }
      break;

    case 15:
      h = mix8((t >> 2) + col * 17 + row * 29);
      if (h < 24) {
        r = 255;
        g = 255;
        b = 255;
      } else {
        r = scale8(base[0], 26);
        g = scale8(base[1], 26);
        b = scale8(base[2], 26);
      }
      break;

    case 16:
      h = mix8((t >> 3) + col * 37 + row * 61);
      if (h < 10) {
        r = 220;
        g = 230;
        b = 255;
      } else if (h < 16) {
        r = 90;
        g = 100;
        b = 130;
      } else {
        inten = 0;
      }
      break;

    case 17:
      h = mix8((t >> 1) + col * 11 + row * 23);
      v = h >> 1;
      k = u8(80 + row * 30);
      r = 255;
      g = scale8(v, k);
      b = v >> 4;
      break;

    case 18:
      v = tri16((t >> 1) + col * 20 + row * 34);
      r = 0;
      g = scale8(90, v);
      b = scale8(255, v);
      break;

    case 19: {
      const c = hue2rgb((4 + row * 26) << 2);
      [r, g, b] = c;
      break;
    }

    case 20: {
      const c = hue2rgb((col * 24 + row * 18 + (t >> 2)) & 1023);
      r = u8((c[0] >> 1) + 128);
      g = u8((c[1] >> 1) + 128);
      b = u8((c[2] >> 1) + 128);
      break;
    }

    case 21:
      h = mix8((t >> 2) + col * 53);
      v = tri16(h + row * 40);
      r = 0;
      g = scale8(255, v);
      b = scale8(40, v);
      break;

    case 22: {
      const r2 = (row + (t >> 6) + col) % 5;
      h = mix8((t >> 4) + col * 41 + r2 * 19);
      if (h < 34) {
        r = 235;
        g = 240;
        b = 255;
      } else {
        r = 0;
        g = 8;
        b = 20;
      }
      break;
    }

    case 23:
      v = tri16(centreDist(col, row) * 70 - t);
      r = scale8(0, v);
      g = scale8(170, v);
      b = scale8(255, v);
      break;

    case 24: {
      v = tri16(t >> 1);
      const c = hue2rgb((col * 72 + t) & 1023);
      r = scale8(c[0], v);
      g = scale8(c[1], v);
      b = scale8(c[2], v);
      break;
    }

    case 25:
      p = (t >> 3) & 0x7f;
      v = 0;
      if (p < 16) v = u8(p * 16);
      else if (p < 30) v = u8(255 - (p - 16) * 12);
      else if (p < 44) v = u8((p - 29) * 16);
      else if (p < 60) v = u8(239 - (p - 44) * 14);
      r = scale8(255, v);
      g = scale8(20, v);
      b = scale8(30, v);
      break;

    case 26: {
      p = (t >> 3) % 28;
      const c2 = col << 1;
      d = p > c2 ? p - c2 : c2 - p;
      v = d > 8 ? 20 : u8(235 - d * 25);
      r = scale8(base[0], v);
      g = scale8(base[1], v);
      b = scale8(base[2], v);
      break;
    }

    case 27: {
      p = (t >> 3) % 10;
      const r2 = row << 1;
      d = p > r2 ? p - r2 : r2 - p;
      v = d > 4 ? 16 : u8(240 - d * 50);
      r = scale8(base[0], v);
      g = scale8(base[1], v);
      b = scale8(base[2], v);
      break;
    }

    case 28:
      d = centreDist(col, row);
      p = (t >> 4) % 8;
      v = d === p ? 255 : d + 1 === p ? 90 : 8;
      r = scale8(0, v);
      g = scale8(210, v);
      b = scale8(255, v);
      break;

    case 29:
      if ((t >> 5) % 5 === row) [r, g, b] = base;
      else inten = 0;
      break;

    case 30:
      h = mix8((t >> 3) + col * 7 + row * 13);
      v = tri16(h * 3 + (t >> 1));
      r = scale8(base[0], v);
      g = scale8(base[1], v);
      b = scale8(base[2], v);
      break;

    case 31:
      if (fade) {
        r = 255;
        g = scale8(fade, 190);
        b = 0;
      } else {
        r = 0;
        g = 10;
        b = 28;
        inten = inten >> 1;
      }
      break;

    case 32:
      if (fade) {
        [r, g, b] = hue2rgb((col * 72 + row * 50 + t) & 1023);
      } else {
        r = 0;
        g = 34;
        b = 90;
        inten = inten >> 2;
      }
      break;

    case 33: {
      const c = hue2rgb((((col * 2 + row * 3) * 128 + (t >> 3)) & 1023) | 0);
      if ((col + row) & 1) {
        [r, g, b] = c;
      } else {
        r = scale8(c[0], 130);
        g = scale8(c[1], 130);
        b = scale8(c[2], 130);
      }
      break;
    }

    case 34:
      /* Custom is handled by the caller, which knows the key index. */
      break;

    default:
      inten = 0;
      break;
  }

  return { i: inten, rgb: [r, g, b] };
}

/** Apply calibration and the 6-bit intensity duty, as poke() does. */
function shade(rgb: Rgb, inten: number, cal: Rgb): Rgb {
  const duty = (inten >> 1) / 63; /* intensity byte -> 6-bit PWM */
  const ch = (v: number, c: number) => {
    const t = (v * c) >> 7;
    return Math.min(255, t) * duty;
  };
  return [
    Math.round(ch(rgb[0], cal[0])),
    Math.round(ch(rgb[1], cal[1])),
    Math.round(ch(rgb[2], cal[2])),
  ];
}

/** Rear-light animation ids, mirroring LED_BACK_* in include/led.h. */
export const BACK_MODES = [
  "Solid",
  "Breathe",
  "Heartbeat",
  "Pulse",
  "Rainbow",
  "Reactive",
  "Strobe",
  "Sparkle",
  "Wave",
] as const;

/** One rear-light sample, a literal port of `rear_state()` in src/led.c.
 *
 *  All six rear LEDs share two commons, so the firmware computes a single
 *  state per frame and paints it into every spare slot.  The preview draws the
 *  same sample on all six segments, which is the only thing the hardware can
 *  actually do.
 */
/** True when the rear light is doing something that changes over time.  The
 *  preview loop has to keep running for these even when the key effect is
 *  static, otherwise the strip would sit frozen on screen.  Direct is the
 *  exception: it paints the six rear segments from the host's frame, so the
 *  strip is part of a still picture there (see `rear_state()`). */
export const rearAnimates = (lighting: Lighting): boolean =>
  lighting.effect !== DIRECT &&
  lighting.backOn !== 0 &&
  lighting.backMode >= 1 &&
  lighting.backMode <= 8;

function rearAt(lighting: Lighting, t: number, fade: number): { i: number; rgb: Rgb } {
  let inten = lighting.backLevel;
  const base = lighting.backRgb;

  if (!lighting.backOn) return { i: 0, rgb: [0, 0, 0] };

  switch (lighting.backMode) {
    case 1: /* breathe */
      inten = (inten * tri16(t >> 1)) >> 8;
      break;
    case 2: {
      /* heartbeat */
      const p = t & 0xff;
      let v = 0;
      if (p < 16) v = 255 - p * 10;
      else if (p >= 28 && p < 44) v = 190 - (p - 28) * 10;
      inten = (inten * v) >> 8;
      break;
    }
    case 3: {
      /* pulse: sharper than a sine, like the firmware's scale8(v, v) */
      const v = tri16(t >> 2);
      inten = (inten * scale8(v, v)) >> 8;
      break;
    }
    case 4: /* rainbow */
      return { i: inten, rgb: hue2rgb((t << 1) & 0xffff) };
    case 5: /* reactive: idle at a third, flare on a keypress */
      inten = (inten * (85 + scale8(fade, 170))) >> 8;
      break;
    case 6: /* strobe: three dark frames in eight */
      if ((t & 7) < 3) inten = 0;
      break;
    case 7: {
      /* sparkle: deterministic hash of the frame clock */
      const h = mix8(t * 7);
      if (h > 208) inten = 255;
      else if (h > 150) inten >>= 1;
      else inten >>= 3;
      break;
    }
    case 8: /* wave: hue drift under a brightness swell */
      return {
        i: (inten * (120 + (tri16(t >> 1) >> 1))) >> 8,
        rgb: hue2rgb((t * 3) & 0xffff),
      };
    default:
      break;
  }

  return { i: inten, rgb: base };
}

/**
 * One preview frame.
 *
 * `t` is the animation clock in quarter-hue units, `fades` is the per-key
 * reactive level (0..255) keyed by LED index.  The result is 69 RGB values in
 * OpenRGB order, already dimmed and calibrated, so the screen shows what the
 * panel will actually emit rather than an idealised version of the effect.
 */
export function previewFrame(
  lighting: Lighting,
  t: number,
  fades: Record<number, number> = {}
): Rgb[] {
  const out: Rgb[] = new Array(LED_COUNT);
  const clock = t & 0xffff;
  const custom = lighting.effect === 34;
  const direct = lighting.effect === DIRECT;

  for (const key of KEYS) {
    if (custom) {
      const c = lighting.custom[key.led] ?? [0, 0, 0];
      out[key.led] =
        c[0] + c[1] + c[2] === 0 ? [0, 0, 0] : shade(c as Rgb, lighting.val >> 1, lighting.cal);
      continue;
    }
    if (direct) {
      /* Direct is a host frame, not an effect: key N is simply colours[N]
       * dimmed by the master brightness, exactly as the board paints it. */
      const c = lighting.colours[key.led] ?? [0, 0, 0];
      out[key.led] = shade(c as Rgb, lighting.val >> 1, lighting.cal);
      continue;
    }
    const { i, rgb } = pixelAt(lighting, key.col, key.row, clock, fades[key.led] ?? 0);
    out[key.led] = shade(rgb, i, lighting.cal);
  }

  /* Rear / side LEDs: driven from the spare matrix slots at their own level,
   * so they are independent of the key effects and of master brightness. */
  let fade = 0;
  for (const v of Object.values(fades)) if (v > fade) fade = v;
  let back: Rgb;
  if (direct) {
    /* The firmware averages the host's six underglow colours into the one
     * state the strip can show (rear_state()). */
    const six = lighting.colours.slice(KEYS.length, LED_COUNT);
    const avg: Rgb = [0, 0, 0];
    if (six.length) {
      for (const c of six) for (let i = 0; i < 3; i++) avg[i] += c[i] ?? 0;
      for (let i = 0; i < 3; i++) avg[i] = Math.round(avg[i] / six.length);
    }
    back = shade(avg, lighting.backOn ? lighting.backLevel : 0, lighting.cal);
  } else {
    const rear = rearAt(lighting, clock, fade);
    back = shade(rear.rgb, rear.i, lighting.cal);
  }
  for (let i = KEYS.length; i < LED_COUNT; i++) out[i] = [...back] as Rgb;

  return out;
}

/**
 * Wire mode for a firmware effect id.  OpenRGB numbers the modes it builds by
 * their position in the board's advertised list, not by their QMK id: Static,
 * Breathing, Cycle All, Hue Wave and Solid Reactive take 1..5, and Direct is
 * created last so it is 6.  Every other effect travels as 100 + its id.
 */
export function wireMode(effect: number): number {
  if (effect === DIRECT) return 6;
  switch (effect) {
    case 1:
      return 1;
    case 2:
      return 2;
    case 3:
      return 3;
    case 5:
      return 4;
    case 6:
      return 5;
    default:
      return 100 + effect;
  }
}

/**
 * Inverse of wireMode, for profiles written before the effect id was stored
 * alongside the wire mode.  Returns null for a value that has no effect.
 */
export function effectForWire(mode: number): number | null {
  if (mode === 6) return DIRECT;
  switch (mode) {
    case 1:
      return 1;
    case 2:
      return 2;
    case 3:
      return 3;
    case 4:
      return 5;
    case 5:
      return 6;
    default:
      return mode >= 100 ? mode - 100 : null;
  }
}
