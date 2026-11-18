/* Key geometry and LED indices.  Shared with host/map.json: the list
 * index is the LED index, so the two files have to stay in step. */

export type KeyDef = {
  id: string;
  legend: string;
  x: number;
  y: number;
  w: number;
  h: number;
  col: number;
  row: number;
  led: number;
  base: string;
  fn: string;
  media: string | null;
};

export const UNITS_X = 15;
export const UNITS_Y = 5;

export const KEYS: KeyDef[] = [
  { id: "ESC", legend: "ESC", x: 0, y: 0, w: 1, h: 1, col: 0, row: 0, led: 0, base: "ESC", fn: "`", media: null },
  { id: "1", legend: "1", x: 1, y: 0, w: 1, h: 1, col: 1, row: 0, led: 1, base: "1", fn: "F1", media: null },
  { id: "2", legend: "2", x: 2, y: 0, w: 1, h: 1, col: 2, row: 0, led: 2, base: "2", fn: "F2", media: null },
  { id: "3", legend: "3", x: 3, y: 0, w: 1, h: 1, col: 3, row: 0, led: 3, base: "3", fn: "F3", media: null },
  { id: "4", legend: "4", x: 4, y: 0, w: 1, h: 1, col: 4, row: 0, led: 4, base: "4", fn: "F4", media: null },
  { id: "5", legend: "5", x: 5, y: 0, w: 1, h: 1, col: 5, row: 0, led: 5, base: "5", fn: "F5", media: null },
  { id: "6", legend: "6", x: 6, y: 0, w: 1, h: 1, col: 6, row: 0, led: 6, base: "6", fn: "F6", media: null },
  { id: "7", legend: "7", x: 7, y: 0, w: 1, h: 1, col: 7, row: 0, led: 7, base: "7", fn: "F7", media: null },
  { id: "8", legend: "8", x: 8, y: 0, w: 1, h: 1, col: 8, row: 0, led: 8, base: "8", fn: "F8", media: null },
  { id: "9", legend: "9", x: 9, y: 0, w: 1, h: 1, col: 9, row: 0, led: 9, base: "9", fn: "F9", media: null },
  { id: "0", legend: "0", x: 10, y: 0, w: 1, h: 1, col: 10, row: 0, led: 10, base: "0", fn: "F10", media: null },
  { id: "MINUS", legend: "−", x: 11, y: 0, w: 1, h: 1, col: 11, row: 0, led: 11, base: "-", fn: "F11", media: null },
  { id: "EQUAL", legend: "=", x: 12, y: 0, w: 1, h: 1, col: 12, row: 0, led: 12, base: "=", fn: "F12", media: null },
  { id: "BSPC", legend: "BACKSPACE", x: 13, y: 0, w: 2, h: 1, col: 13, row: 0, led: 13, base: "BACKSPACE", fn: "DELETE", media: null },
  { id: "TAB", legend: "TAB", x: 0, y: 1, w: 1.50, h: 1, col: 0, row: 1, led: 14, base: "TAB", fn: "—", media: null },
  { id: "Q", legend: "Q", x: 1.50, y: 1, w: 1, h: 1, col: 1, row: 1, led: 15, base: "Q", fn: "—", media: "MUTE" },
  { id: "W", legend: "W", x: 2.50, y: 1, w: 1, h: 1, col: 2, row: 1, led: 16, base: "W", fn: "—", media: "VOL −" },
  { id: "E", legend: "E", x: 3.50, y: 1, w: 1, h: 1, col: 3, row: 1, led: 17, base: "E", fn: "—", media: "VOL +" },
  { id: "R", legend: "R", x: 4.50, y: 1, w: 1, h: 1, col: 4, row: 1, led: 18, base: "R", fn: "—", media: "TRACK −" },
  { id: "T", legend: "T", x: 5.50, y: 1, w: 1, h: 1, col: 5, row: 1, led: 19, base: "T", fn: "—", media: "TRACK +" },
  { id: "Y", legend: "Y", x: 6.50, y: 1, w: 1, h: 1, col: 6, row: 1, led: 20, base: "Y", fn: "—", media: "PLAY / PAUSE" },
  { id: "U", legend: "U", x: 7.50, y: 1, w: 1, h: 1, col: 7, row: 1, led: 21, base: "U", fn: "INSERT", media: null },
  { id: "I", legend: "I", x: 8.50, y: 1, w: 1, h: 1, col: 8, row: 1, led: 22, base: "I", fn: "PRINT SCR", media: null },
  { id: "O", legend: "O", x: 9.50, y: 1, w: 1, h: 1, col: 9, row: 1, led: 23, base: "O", fn: "SCROLL LCK", media: null },
  { id: "P", legend: "P", x: 10.50, y: 1, w: 1, h: 1, col: 10, row: 1, led: 24, base: "P", fn: "PAUSE", media: null },
  { id: "LBRC", legend: "[", x: 11.50, y: 1, w: 1, h: 1, col: 11, row: 1, led: 25, base: "[", fn: "—", media: null },
  { id: "RBRC", legend: "]", x: 12.50, y: 1, w: 1, h: 1, col: 12, row: 1, led: 26, base: "]", fn: "—", media: null },
  { id: "BSLS", legend: "\\", x: 13.50, y: 1, w: 1.50, h: 1, col: 13, row: 1, led: 27, base: "\\", fn: "—", media: null },
  { id: "CAPS", legend: "CAPS", x: 0, y: 2, w: 1.75, h: 1, col: 0, row: 2, led: 28, base: "CAPS", fn: "—", media: null },
  { id: "A", legend: "A", x: 1.75, y: 2, w: 1, h: 1, col: 1, row: 2, led: 29, base: "A", fn: "—", media: null },
  { id: "S", legend: "S", x: 2.75, y: 2, w: 1, h: 1, col: 2, row: 2, led: 30, base: "S", fn: "—", media: null },
  { id: "D", legend: "D", x: 3.75, y: 2, w: 1, h: 1, col: 3, row: 2, led: 31, base: "D", fn: "—", media: null },
  { id: "F", legend: "F", x: 4.75, y: 2, w: 1, h: 1, col: 4, row: 2, led: 32, base: "F", fn: "—", media: null },
  { id: "G", legend: "G", x: 5.75, y: 2, w: 1, h: 1, col: 5, row: 2, led: 33, base: "G", fn: "—", media: null },
  { id: "H", legend: "H", x: 6.75, y: 2, w: 1, h: 1, col: 6, row: 2, led: 34, base: "H", fn: "—", media: null },
  { id: "J", legend: "J", x: 7.75, y: 2, w: 1, h: 1, col: 7, row: 2, led: 35, base: "J", fn: "—", media: null },
  { id: "K", legend: "K", x: 8.75, y: 2, w: 1, h: 1, col: 8, row: 2, led: 36, base: "K", fn: "—", media: null },
  { id: "L", legend: "L", x: 9.75, y: 2, w: 1, h: 1, col: 9, row: 2, led: 37, base: "L", fn: "NUM LCK", media: null },
  { id: "SCLN", legend: ";", x: 10.75, y: 2, w: 1, h: 1, col: 10, row: 2, led: 38, base: ";", fn: "—", media: null },
  { id: "QUOT", legend: "'", x: 11.75, y: 2, w: 1, h: 1, col: 11, row: 2, led: 39, base: "'", fn: "—", media: null },
  { id: "ENT", legend: "ENTER", x: 12.75, y: 2, w: 2.25, h: 1, col: 12, row: 2, led: 40, base: "ENTER", fn: "—", media: null },
  { id: "LSFT", legend: "SHIFT", x: 0, y: 3, w: 2.25, h: 1, col: 0, row: 3, led: 41, base: "L SHIFT", fn: "—", media: null },
  { id: "Z", legend: "Z", x: 2.25, y: 3, w: 1, h: 1, col: 1, row: 3, led: 42, base: "Z", fn: "—", media: null },
  { id: "X", legend: "X", x: 3.25, y: 3, w: 1, h: 1, col: 2, row: 3, led: 43, base: "X", fn: "—", media: null },
  { id: "C", legend: "C", x: 4.25, y: 3, w: 1, h: 1, col: 3, row: 3, led: 44, base: "C", fn: "—", media: null },
  { id: "V", legend: "V", x: 5.25, y: 3, w: 1, h: 1, col: 4, row: 3, led: 45, base: "V", fn: "—", media: null },
  { id: "B", legend: "B", x: 6.25, y: 3, w: 1, h: 1, col: 5, row: 3, led: 46, base: "B", fn: "—", media: null },
  { id: "N", legend: "N", x: 7.25, y: 3, w: 1, h: 1, col: 6, row: 3, led: 47, base: "N", fn: "—", media: null },
  { id: "M", legend: "M", x: 8.25, y: 3, w: 1, h: 1, col: 7, row: 3, led: 48, base: "M", fn: "—", media: null },
  { id: "COMM", legend: ",", x: 9.25, y: 3, w: 1, h: 1, col: 8, row: 3, led: 49, base: ",", fn: "—", media: null },
  { id: "DOT", legend: ".", x: 10.25, y: 3, w: 1, h: 1, col: 9, row: 3, led: 50, base: ".", fn: "—", media: null },
  { id: "SLSH", legend: "/", x: 11.25, y: 3, w: 1, h: 1, col: 10, row: 3, led: 51, base: "/", fn: "—", media: null },
  { id: "UP", legend: "↑", x: 12.25, y: 3, w: 1, h: 1, col: 12, row: 3, led: 52, base: "UP", fn: "—", media: null },
  { id: "RSFT", legend: "SHIFT", x: 13.25, y: 3, w: 1.75, h: 1, col: 11, row: 3, led: 53, base: "R SHIFT", fn: "—", media: null },
  { id: "LCTL", legend: "CTRL", x: 0, y: 4, w: 1.25, h: 1, col: 0, row: 4, led: 54, base: "L CTRL", fn: "—", media: null },
  { id: "LGUI", legend: "WIN", x: 1.25, y: 4, w: 1.25, h: 1, col: 1, row: 4, led: 55, base: "L GUI", fn: "—", media: null },
  { id: "LALT", legend: "ALT", x: 2.50, y: 4, w: 1.25, h: 1, col: 2, row: 4, led: 56, base: "L ALT", fn: "—", media: null },
  { id: "SPC", legend: "", x: 3.75, y: 4, w: 6.25, h: 1, col: 3, row: 4, led: 57, base: "SPACE", fn: "—", media: null },
  { id: "RALT", legend: "ALT", x: 10, y: 4, w: 1.25, h: 1, col: 4, row: 4, led: 58, base: "R ALT", fn: "—", media: null },
  { id: "LEFT", legend: "←", x: 11.25, y: 4, w: 1, h: 1, col: 5, row: 4, led: 59, base: "LEFT", fn: "—", media: null },
  { id: "DOWN", legend: "↓", x: 12.25, y: 4, w: 1, h: 1, col: 6, row: 4, led: 60, base: "DOWN", fn: "—", media: null },
  { id: "RGHT", legend: "→", x: 13.25, y: 4, w: 1, h: 1, col: 7, row: 4, led: 61, base: "RIGHT", fn: "—", media: null },
  { id: "FN", legend: "FN", x: 14.25, y: 4, w: 0.75, h: 1, col: 11, row: 4, led: 62, base: "—", fn: "—", media: null },
];

export type StripDef = { id: string; led: number; x: number; w: number };

/* The six rear / side LEDs, in OpenRGB zone order. */
export const STRIP: StripDef[] = [
  { id: "U1", led: 63, x: 0, w: 1 },
  { id: "U2", led: 64, x: 1, w: 1 },
  { id: "U3", led: 65, x: 2, w: 1 },
  { id: "U4", led: 66, x: 3, w: 1 },
  { id: "U5", led: 67, x: 4, w: 1 },
  { id: "U6", led: 68, x: 5, w: 1 },
];

export const KEY_COUNT = KEYS.length;
export const LED_COUNT = 69;
