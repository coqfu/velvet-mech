/* HID keyboard usages, and the factory base-layer table from src/keymap.c.
 *
 * The usage numbers are the ones the firmware sends in its NKRO bitmap, so
 * the remap editor and the macro recorder both speak the same language as the
 * board - no scancode translation anywhere.
 */

/** Factory base layer, in K_* / LED order (src/keymap.c kbd_layer0). */
export const FACTORY_USAGE: number[] = [
  0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a,
  0x2b, 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x31,
  0x39, 0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x28,
  0xe1, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0x52, 0xe5,
  0xe0, 0xe3, 0xe2, 0x2c, 0xe6, 0x50, 0x51, 0x4f, 0x00,
];

export const USAGE = {
  NONE: 0x00,
  FACTORY: 0xff,
  LCTRL: 0xe0,
  LSHIFT: 0xe1,
  LALT: 0xe2,
  LGUI: 0xe3,
  RCTRL: 0xe4,
  RSHIFT: 0xe5,
  RALT: 0xe6,
  RGUI: 0xe7,
} as const;

const NAMES: Record<number, string> = {
  0x00: "Disabled",
  0x28: "Enter",
  0x29: "Esc",
  0x2a: "Backspace",
  0x2b: "Tab",
  0x2c: "Space",
  0x2d: "−",
  0x2e: "=",
  0x2f: "[",
  0x30: "]",
  0x31: "\\",
  0x32: "#",
  0x33: ";",
  0x34: "'",
  0x35: "`",
  0x36: ",",
  0x37: ".",
  0x38: "/",
  0x39: "Caps Lock",
  0x46: "Print Screen",
  0x47: "Scroll Lock",
  0x48: "Pause",
  0x49: "Insert",
  0x4a: "Home",
  0x4b: "Page Up",
  0x4c: "Delete",
  0x4d: "End",
  0x4e: "Page Down",
  0x4f: "Right",
  0x50: "Left",
  0x51: "Down",
  0x52: "Up",
  0xe0: "Left Ctrl",
  0xe1: "Left Shift",
  0xe2: "Left Alt",
  0xe3: "Left GUI",
  0xe4: "Right Ctrl",
  0xe5: "Right Shift",
  0xe6: "Right Alt",
  0xe7: "Right GUI",
};

for (let i = 0; i < 26; i++) NAMES[0x04 + i] = String.fromCharCode(65 + i);
for (let i = 0; i < 9; i++) NAMES[0x1e + i] = String(i + 1);
NAMES[0x27] = "0";
for (let i = 0; i < 12; i++) NAMES[0x3a + i] = `F${i + 1}`;

export const usageName = (u: number): string =>
  u === USAGE.FACTORY ? "Factory" : NAMES[u] ?? `0x${u.toString(16).toUpperCase().padStart(2, "0")}`;

/* Keycap captions.  Much shorter than usageName(), because they have to fit
 * inside a 1u key: the board picture relabels itself when a key is remapped,
 * so what is printed on screen is what the key actually sends. */
const LEGEND: Record<number, string> = {
  0x00: "OFF",
  0x28: "ENTER",
  0x29: "ESC",
  0x2a: "BACKSPACE",
  0x2b: "TAB",
  0x2c: "SPACE",
  0x2d: "−",
  0x2e: "=",
  0x2f: "[",
  0x30: "]",
  0x31: "\\",
  0x32: "#",
  0x33: ";",
  0x34: "'",
  0x35: "`",
  0x36: ",",
  0x37: ".",
  0x38: "/",
  0x39: "CAPS",
  0x46: "PRT SCR",
  0x47: "SCR LCK",
  0x48: "PAUSE",
  0x49: "INSERT",
  0x4a: "HOME",
  0x4b: "PG UP",
  0x4c: "DELETE",
  0x4d: "END",
  0x4e: "PG DN",
  0x4f: "→",
  0x50: "←",
  0x51: "↓",
  0x52: "↑",
  0xe0: "CTRL",
  0xe1: "SHIFT",
  0xe2: "ALT",
  0xe3: "WIN",
  0xe4: "CTRL",
  0xe5: "SHIFT",
  0xe6: "ALT",
  0xe7: "WIN",
};

/** Usage -> the text printed on that key.  A disabled key reads "OFF". */
export const usageLegend = (u: number): string => {
  if (u >= 0x04 && u <= 0x1d) return String.fromCharCode(0x41 + u - 0x04);
  if (u >= 0x1e && u <= 0x26) return String(u - 0x1e + 1);
  if (u === 0x27) return "0";
  if (u >= 0x3a && u <= 0x45) return `F${u - 0x3a + 1}`;
  return LEGEND[u] ?? `0x${u.toString(16).toUpperCase().padStart(2, "0")}`;
};

/** Every usage the editor offers, grouped for the picker. */
export const USAGE_GROUPS: { label: string; items: number[] }[] = [
  { label: "Letters", items: Array.from({ length: 26 }, (_, i) => 0x04 + i) },
  { label: "Digits", items: [0x27, ...Array.from({ length: 9 }, (_, i) => 0x1e + i)] },
  {
    label: "Punctuation",
    items: [0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38],
  },
  { label: "Modifiers", items: [0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7] },
  { label: "Function", items: Array.from({ length: 12 }, (_, i) => 0x3a + i) },
  {
    label: "Navigation",
    items: [0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52],
  },
  { label: "System", items: [0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x39, 0x46, 0x47, 0x48] },
];

/** KeyboardEvent -> HID usage, for the macro recorder. */
const CODE_TO_USAGE: Record<string, number> = {
  Escape: 0x29,
  Backquote: 0x35,
  Minus: 0x2d,
  Equal: 0x2e,
  Backspace: 0x2a,
  Tab: 0x2b,
  BracketLeft: 0x2f,
  BracketRight: 0x30,
  Backslash: 0x31,
  CapsLock: 0x39,
  Semicolon: 0x33,
  Quote: 0x34,
  Enter: 0x28,
  Comma: 0x36,
  Period: 0x37,
  Slash: 0x38,
  Space: 0x2c,
  PrintScreen: 0x46,
  ScrollLock: 0x47,
  Pause: 0x48,
  Insert: 0x49,
  Home: 0x4a,
  PageUp: 0x4b,
  Delete: 0x4c,
  End: 0x4d,
  PageDown: 0x4e,
  ArrowRight: 0x4f,
  ArrowLeft: 0x50,
  ArrowDown: 0x51,
  ArrowUp: 0x52,
  ShiftLeft: 0xe1,
  ShiftRight: 0xe5,
  ControlLeft: 0xe0,
  ControlRight: 0xe4,
  AltLeft: 0xe2,
  AltRight: 0xe6,
  MetaLeft: 0xe3,
  MetaRight: 0xe7,
};

for (let i = 0; i < 26; i++) CODE_TO_USAGE[`Key${String.fromCharCode(65 + i)}`] = 0x04 + i;
for (let i = 0; i < 10; i++) CODE_TO_USAGE[`Digit${i}`] = i === 0 ? 0x27 : 0x1e + i - 1;
for (let i = 1; i <= 12; i++) CODE_TO_USAGE[`F${i}`] = 0x3a + i - 1;

export const usageFromCode = (code: string): number | null => CODE_TO_USAGE[code] ?? null;
