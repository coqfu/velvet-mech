export type Rgb = [number, number, number];

/** Direct is not an effect: it paints the LED buffer over the OpenRGB path. */
export const DIRECT = -1;

/** One recorded macro: the trigger key plus (usage, hold) steps. */
export type MacroStep = { usage: number; hold: number };

export type Macro = {
  /** HID usage of the trigger key, 0xFF when the slot is empty. */
  key: number;
  len: number;
  steps: MacroStep[];
};

/**
 * Everything the board can actually be told to do.  There is no field here
 * that the firmware cannot act on - the honesty rule is enforced at the type
 * level, so a control cannot be added without a command to back it.
 */
export type Lighting = {
  /** Firmware effect id 0..34; -1 means Direct (a per-key picture). */
  effect: number;
  /** OpenRGB wire mode the server addresses this effect with. */
  mode: number;
  /** 0..255 firmware HSV hue of the base colour. */
  hue: number;
  /** 0..255. */
  sat: number;
  /** 0..254 in steps of 2: the firmware keeps a 7-bit brightness (val >> 1). */
  val: number;
  /** 0..255 raw speed byte: led_speed = 1 + (speed >> 5). */
  speed: number;
  /** 63 key LEDs + 6 rear, OpenRGB order. */
  colours: Rgb[];
  /** 63 per-key colours for the Custom effect. */
  custom: Rgb[];
  /** Rear / side LEDs on or off. */
  backOn: number;
  /** 0..127 rear brightness. */
  backLevel: number;
  /** Rear colour. */
  backRgb: Rgb;
  /** Per-channel white balance, 128 = unity. */
  cal: Rgb;
  /** 1..6 stable scan frames before a key registers. */
  debounce: number;
  /** 1 = hold keys a frame longer for game chords. */
  gaming: number;
  /** Start-up animation: 0 off, 1 sweep, 2 rainbow wipe, 3 fade, 4 scan,
   *  5 pulse. */
  bootMode: number;
  /** 0 fast, 1 normal, 2 slow. */
  bootPace: number;
  /** Rear animation: 0 solid, 1 breathe, 2 heartbeat, 3 pulse, 4 rainbow,
   *  5 reactive. */
  backMode: number;
  /** 63 HID usages; 0xFF factory, 0x00 disabled. */
  remap: number[];
  /** Four macro slots, played by the board itself. */
  macros: Macro[];
};

export type DeviceInfo = {
  connected: boolean;
  released?: boolean;
  path: string | null;
  vendorId: number | null;
  productId: number | null;
  name: string;
  vendor: string;
  qmk: string;
  protocol: number | null;
  keyCount: number;
  ledCount: number;
  underglowCount: number;
  modes: number[];
};

export type AudioMode =
  | "spectrum"
  | "pulse"
  | "wave"
  | "meter"
  | "spark"
  | "ripple"
  | "comet";
export type AudioPalette = "prism" | "mono" | "heat" | "ocean" | "violet" | "sunset";

/** Sound-reactive lighting is rendered on the host, not by the firmware:
 *  the board has no microphone.  These fields describe the capture and the
 *  one frame stream that is actually sent over the LED path. */
export type AudioState = {
  params: {
    mode: AudioMode;
    palette: AudioPalette;
    gain: number;
    ceiling: number;
    source?: string;
    error?: string;
  };
  running: boolean;
  /** Monitor source being recorded, e.g. sink.monitor. */
  source: string | null;
  error: string | null;
  /** Why the last run stopped, when it was a deliberate action. */
  note: string | null;
  /** 0..1 smoothed RMS. */
  level: number;
  /** 14 logarithmic bands, 0..1. */
  bands: number[];
  /** 0..1, decays after a kick. */
  beat: number;
  /** Frames per second actually streamed. */
  fps: number;
  modes: { id: AudioMode; desc: string }[];
  palettes: { id: AudioPalette; desc: string }[];
  sources: { id: string; state: string }[];
  targetFps: number;
};

export type Diag = {
  /** Measured frame rate.  Null for the first sample after a board restart. */
  fps: number | null;
  renderMs: number;
  frames: number;
  irqs: number;
  dmaBase: number;
  directActive: boolean;
  on: boolean;
  backLight: boolean;
  brightness: number;
  effect: number;
  speed: number;
  phase: number;
  /** PCON reset flag: power-on / software / watchdog / pin.  Null on older
   *  firmware that does not send the byte. */
  reset: string | null;
};

export type FlashJob = {
  state: "idle" | "waiting" | "writing" | "done" | "failed";
  message: string;
  output: string;
  ok: boolean | null;
  attempts: number;
  waited: number;
};

export type Status = {
  device: DeviceInfo;
  lighting: Lighting;
  diag: Diag | null;
};

export type Profile = {
  name: string;
  saved: string;
  /* A profile is a lighting preset: it carries the fields Apply writes, not
   * the keymap or macros, which have their own pages and their own writes. */
  lighting: Partial<Lighting>;
  keyset?: number[];
};
