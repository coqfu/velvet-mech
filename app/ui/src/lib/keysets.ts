import { KEYS } from "./keyboard-data";

const byId = (ids: string[]) => ids.map((id) => KEYS.find((k) => k.id === id)!.led);

export type Keyset = { name: string; indices: number[] };

export const PRESETS: Keyset[] = [
  { name: "All keys", indices: KEYS.map((k) => k.led) },
  { name: "Alphas", indices: byId("QWERTYUIOPASDFGHJKLZXCVBNM".split("")) },
  { name: "WASD", indices: byId(["W", "A", "S", "D"]) },
  { name: "Modifiers", indices: byId(["LSFT", "RSFT", "LCTL", "LALT", "RALT", "LGUI"]) },
  { name: "Arrows", indices: byId(["UP", "LEFT", "DOWN", "RGHT"]) },
  { name: "Number row", indices: KEYS.filter((k) => k.row === 0).map((k) => k.led) },
  { name: "Home row", indices: KEYS.filter((k) => k.row === 2).map((k) => k.led) },
  { name: "Bottom row", indices: KEYS.filter((k) => k.row === 4).map((k) => k.led) },
];
