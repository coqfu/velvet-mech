import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { api } from "../lib/api";
import { clamp, hsvToRgb } from "../lib/colour";
import { KEYS, KEY_COUNT, LED_COUNT } from "../lib/keyboard-data";
import type { FlashJob, Lighting, Macro, MacroStep, Profile, Rgb, Status } from "../lib/types";
import { DIRECT } from "../lib/types";

export type Page =
  | "lighting"
  | "effects"
  | "sound"
  | "keymap"
  | "macros"
  | "profiles"
  | "advanced";

export type Toast = { id: number; msg: string; kind: "ok" | "error" | "warn" | "info" };

const blank = (n: number): Rgb[] => Array.from({ length: n }, () => [0, 0, 0] as Rgb);

/** Fields of Lighting that are board settings rather than part of the frame
 *  being painted.  Changing one of these is a device action, so the store
 *  writes it immediately instead of waiting for APPLY: toggling the rear light
 *  off must turn it off now, not on the next Apply. */
const CONFIG_KEYS = ["backOn", "backLevel", "backRgb", "backMode"] as const;

/** Board settings the 0x79 packet carries.  A write can contain just the
 *  fields a control actually changed; the service fills the rest from the
 *  state it already holds. */
type ConfigPatch = Partial<
  Pick<
    Lighting,
    "backOn" | "backLevel" | "backRgb" | "cal" | "debounce" | "gaming" | "bootMode" | "bootPace" | "backMode"
  >
>;

const configOf = (src: Lighting): ConfigPatch => ({
  backOn: src.backOn,
  backLevel: src.backLevel,
  backRgb: src.backRgb,
  cal: src.cal,
  debounce: src.debounce,
  gaming: src.gaming,
  bootMode: src.bootMode,
  bootPace: src.bootPace,
  backMode: src.backMode,
});

/** Direct-mode seed: a cool horizontal ramp so the board is never a void. */
function seedColours(): Rgb[] {
  const out = blank(LED_COUNT);
  for (const k of KEYS) {
    const hue = 150 + k.col * 9;
    out[k.led] = hsvToRgb(hue, 190, 220);
  }
  return out;
}

/** Custom-mode seed: the same ramp, one colour per key. */
function seedCustom(): Rgb[] {
  const out = blank(KEY_COUNT);
  for (const k of KEYS) out[k.led] = hsvToRgb(150 + k.col * 9, 190, 220);
  return out;
}

export const DEFAULT_LIGHTING: Lighting = {
  effect: 3,
  mode: 3,
  hue: 20,
  sat: 255,
  val: 160,
  speed: 240,
  colours: seedColours(),
  custom: seedCustom(),
  backOn: 1,
  backLevel: 127,
  backRgb: [255, 255, 255],
  cal: [128, 128, 128],
  debounce: 2,
  gaming: 0,
  bootMode: 1,
  bootPace: 1,
  backMode: 0,
  remap: Array.from({ length: KEY_COUNT }, () => 0xff),
  macros: Array.from({ length: 4 }, () => ({ key: 0xff, len: 0, steps: [] as MacroStep[] })),
};

const clone = (l: Lighting): Lighting => ({
  ...l,
  colours: l.colours.map((c) => [...c] as Rgb),
  custom: l.custom.map((c) => [...c] as Rgb),
  backRgb: [...l.backRgb] as Rgb,
  cal: [...l.cal] as Rgb,
  remap: [...l.remap],
  macros: l.macros.map((m) => ({ ...m, steps: m.steps.map((s) => ({ ...s })) })),
});

const sameRgb = (a: Rgb, b: Rgb) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];

/** Dirty = anything the firmware would act on has changed. */
const same = (a: Lighting, b: Lighting) =>
  a.effect === b.effect &&
  a.hue === b.hue &&
  a.sat === b.sat &&
  a.val === b.val &&
  /* led_speed is one of eight rates, and the board reports the byte with a
   * spare bit set (GET_STATE returns ((led_speed - 1) << 5) | 0x10).  Compare
   * the rate rather than the byte, or setting the speed to the value it
   * already is would leave the workspace marked "Unapplied" for ever. */
  a.speed >> 5 === b.speed >> 5 &&
  a.backOn === b.backOn &&
  a.backLevel === b.backLevel &&
  a.debounce === b.debounce &&
  a.gaming === b.gaming &&
  a.bootMode === b.bootMode &&
  a.bootPace === b.bootPace &&
  a.backMode === b.backMode &&
  sameRgb(a.backRgb, b.backRgb) &&
  sameRgb(a.cal, b.cal) &&
  a.colours.every((c, i) => sameRgb(c, b.colours[i])) &&
  a.custom.every((c, i) => sameRgb(c, b.custom[i])) &&
  a.remap.every((v, i) => v === b.remap[i]) &&
  a.macros.every(
    (m, i) =>
      m.key === b.macros[i].key &&
      m.len === b.macros[i].len &&
      m.steps.every((s, j) => s.usage === b.macros[i].steps[j]?.usage && s.hold === b.macros[i].steps[j]?.hold)
  );

/** Server payload -> the shape the UI edits. */
function fromServer(raw: Partial<Lighting> & { direct?: boolean; macros?: unknown[] }): Lighting {
  /* The service returns steps as {usage, hold}; keep accepting the older
   * pair form so a stale server build cannot turn every step into NaN. */
  const oneStep = (s: MacroStep | [number, number]) =>
    Array.isArray(s)
      ? { usage: Number(s[0]) || 0, hold: Number(s[1]) || 0 }
      : { usage: Number(s?.usage) || 0, hold: Number(s?.hold) || 0 };

  const macros = Array.isArray(raw.macros)
    ? (raw.macros as { key: number; len: number; steps: (MacroStep | [number, number])[] }[]).map((m) => ({
        key: m.key,
        len: m.len,
        steps: (m.steps ?? []).map(oneStep),
      }))
    : DEFAULT_LIGHTING.macros;

  return {
    ...DEFAULT_LIGHTING,
    ...raw,
    effect: raw.direct ? DIRECT : typeof raw.effect === "number" ? raw.effect : DEFAULT_LIGHTING.effect,
    mode: typeof raw.mode === "number" ? raw.mode : DEFAULT_LIGHTING.mode,
    /* led_brightness is 7-bit and reads back as value << 1, so only even
     * values exist on the board.  Snap a profile saved by an older build. */
    val: typeof raw.val === "number" ? Math.min(254, raw.val & 0xfe) : DEFAULT_LIGHTING.val,
    colours: Array.isArray(raw.colours) && raw.colours.length === LED_COUNT
      ? (raw.colours as Rgb[])
      : DEFAULT_LIGHTING.colours,
    custom: Array.isArray(raw.custom) && raw.custom.length === KEY_COUNT
      ? (raw.custom as Rgb[])
      : DEFAULT_LIGHTING.custom,
    remap: Array.isArray(raw.remap) && raw.remap.length === KEY_COUNT
      ? [...raw.remap]
      : DEFAULT_LIGHTING.remap,
    macros: macros.length === 4 ? macros : DEFAULT_LIGHTING.macros,
  };
}

type Ctx = {
  page: Page;
  setPage: (p: Page) => void;
  status: Status | null;
  connected: boolean;
  draft: Lighting;
  applied: Lighting;
  dirty: boolean;
  selection: number[];
  setSelection: (next: number[]) => void;
  select: (indices: number[], mode?: "replace" | "add" | "toggle") => void;
  selectKey: (key: string) => number[];
  selectAll: () => void;
  clearSelection: () => void;
  invertSelection: () => void;
  setColour: (rgb: Rgb) => void;
  paint: (indices: number[], rgb: Rgb) => void;
  brush: Rgb;
  setBrush: (rgb: Rgb) => void;
  tool: "select" | "paint";
  setTool: (t: "select" | "paint") => void;
  setLighting: (patch: Partial<Omit<Lighting, "colours" | "custom" | "remap" | "macros">>) => void;
  setEffect: (effect: number) => void;
  setConfig: (
    patch: Partial<
      Pick<
        Lighting,
        "backOn" | "backLevel" | "backRgb" | "cal" | "debounce" | "gaming" | "bootMode" | "bootPace" | "backMode"
      >
    >
  ) => void;
  setRemap: (index: number, usage: number) => Promise<void>;
  remapMany: (indices: number[], usage: number) => Promise<void>;
  resetRemap: () => Promise<void>;
  applyConfig: () => Promise<boolean>;
  bootPreview: () => Promise<void>;
  saveMacro: (slot: number, key: number, steps: MacroStep[]) => Promise<boolean>;
  apply: () => Promise<void>;
  revert: () => void;
  refresh: () => Promise<void>;
  saveFlash: () => Promise<void>;
  enterBootloader: () => Promise<void>;
  flash: () => Promise<void>;
  flashJob: FlashJob;
  profiles: Profile[];
  saveProfile: (name: string) => Promise<boolean>;
  loadProfile: (name: string) => Promise<void>;
  deleteProfile: (name: string) => Promise<void>;
  keysets: Record<string, number[]>;
  saveKeyset: (name: string, indices: number[]) => Promise<boolean>;
  deleteKeyset: (name: string) => Promise<void>;
  toasts: Toast[];
  toast: (msg: string, kind?: Toast["kind"]) => void;
  busy: boolean;
};

const Store = createContext<Ctx | null>(null);

export function StoreProvider({ children }: { children: ReactNode }) {
  const [page, setPage] = useState<Page>("lighting");
  const [status, setStatus] = useState<Status | null>(null);
  const [draft, setDraft] = useState<Lighting>(() => clone(DEFAULT_LIGHTING));
  const [applied, setApplied] = useState<Lighting>(() => clone(DEFAULT_LIGHTING));
  const [selection, setSelection] = useState<number[]>([]);
  const [profiles, setProfiles] = useState<Profile[]>([]);
  const [keysets, setKeysets] = useState<Record<string, number[]>>({});
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [busy, setBusy] = useState(false);
  const [brush, setBrush] = useState<Rgb>([255, 245, 235]);
  const [tool, setTool] = useState<"select" | "paint">("select");

  const toastId = useRef(1);
  const toast = useCallback((msg: string, kind: Toast["kind"] = "info") => {
    const id = toastId.current++;
    setToasts((t) => [...t.slice(-3), { id, msg, kind }]);
    window.setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 3600);
  }, []);

  const dirty = useMemo(() => !same(draft, applied), [draft, applied]);
  const dirtyRef = useRef(dirty);
  dirtyRef.current = dirty;

  /* The newest draft, including a patch that has not rendered yet.  Two
   * controls touched in the same tick must not overwrite each other. */
  const draftRef = useRef(draft);
  draftRef.current = draft;

  const writeConfig = useCallback(async (patch: ConfigPatch) => {
    await api.config(patch);
    setApplied((a) => ({
      ...a,
      ...patch,
      /* Copy the tuples so a later draft edit cannot mutate what the board
       * is believed to hold. */
      ...(patch.backRgb ? { backRgb: [...patch.backRgb] as Rgb } : {}),
      ...(patch.cal ? { cal: [...patch.cal] as Rgb } : {}),
    }));
  }, []);

  /** Write the whole config block (used after Apply, which must not leave any
   *  field behind when the board's state has drifted). */
  const pushConfig = useCallback(
    (src: Lighting) => writeConfig(configOf(src)),
    [writeConfig]
  );

  /* Rear-light controls are board settings, not part of the frame being
   * painted, so they are written as soon as they are touched.  The short hold
   * keeps a dragged slider from sending one packet per pixel while still
   * landing the final value.  Only the fields that were actually touched are
   * sent, so a stray rear-light click cannot commit a debounce or calibration
   * edit that is still waiting for its own Write button. */
  const configTimer = useRef<number | null>(null);
  const configPending = useRef<ConfigPatch | null>(null);
  const queueConfig = useCallback(
    (patch: ConfigPatch) => {
      configPending.current = { ...configPending.current, ...patch };
      if (configTimer.current !== null) return;
      configTimer.current = window.setTimeout(() => {
        configTimer.current = null;
        const snap = configPending.current;
        configPending.current = null;
        if (!snap) return;
        void writeConfig(snap).catch((err) => {
          toast(err instanceof Error ? err.message : "config write failed", "error");
        });
      }, 140);
    },
    [writeConfig, toast]
  );

  const adopt = useCallback((next: Lighting) => {
    setApplied(clone(next));
    setDraft((cur) => (dirtyRef.current ? cur : clone(next)));
  }, []);

  const refresh = useCallback(async () => {
    try {
      const s = await api.status();
      setStatus(s);
      if (s.device.connected) adopt(fromServer(s.lighting));
    } catch {
      setStatus((cur) => (cur ? { ...cur, device: { ...cur.device, connected: false } } : cur));
    }
  }, [adopt]);

  const loadProfiles = useCallback(async () => {
    try {
      const [{ profiles: p }, { keysets: k }] = await Promise.all([api.profiles(), api.keysets()]);
      setProfiles(p);
      setKeysets(k);
    } catch {
      /* the service may not be up yet; the UI still works offline */
    }
  }, []);

  useEffect(() => {
    void refresh();
    void loadProfiles();
    const timer = window.setInterval(() => void refresh(), 6000);
    return () => window.clearInterval(timer);
  }, [refresh, loadProfiles]);

  const select = useCallback((indices: number[], mode: "replace" | "add" | "toggle" = "replace") => {
    setSelection((cur) => {
      if (mode === "replace") return [...new Set(indices)].sort((a, b) => a - b);
      const next = new Set(cur);
      for (const i of indices) {
        if (mode === "toggle") {
          if (next.has(i)) next.delete(i);
          else next.add(i);
        } else next.add(i);
      }
      return [...next].sort((a, b) => a - b);
    });
  }, []);

  const selectKey = useCallback((id: string) => {
    const k = KEYS.find((x) => x.id === id);
    return k ? [k.led] : [];
  }, []);

  const selectAll = useCallback(() => setSelection(KEYS.map((k) => k.led)), []);
  const clearSelection = useCallback(() => setSelection([]), []);
  const invertSelection = useCallback(() => {
    setSelection((cur) => {
      const set = new Set(cur);
      return KEYS.map((k) => k.led).filter((i) => !set.has(i));
    });
  }, []);

  const setLighting = useCallback(
    (patch: Partial<Omit<Lighting, "colours" | "custom" | "remap" | "macros">>) => {
      setDraft((cur) => ({ ...cur, ...patch }));
      /* The queue only needs the config fields, which paint() never touches.
       * Send only the ones this patch changed so a rear-light tweak cannot
       * commit an unrelated edit waiting on the Advanced page. */
      if (CONFIG_KEYS.some((k) => k in patch)) {
        const next: ConfigPatch = {};
        if (patch.backOn !== undefined) next.backOn = patch.backOn;
        if (patch.backLevel !== undefined) next.backLevel = patch.backLevel;
        if (patch.backRgb !== undefined) next.backRgb = patch.backRgb;
        if (patch.backMode !== undefined) next.backMode = patch.backMode;
        queueConfig(next);
      }
    },
    [queueConfig]
  );

  const setEffect = useCallback((effect: number) => {
    setDraft((cur) => ({ ...cur, effect }));
  }, []);

  const setConfig = useCallback(
    (patch: Partial<Pick<Lighting, "backOn" | "backLevel" | "backRgb" | "cal" | "debounce" | "gaming" | "bootMode" | "bootPace" | "backMode">>) => {
      setDraft((cur) => ({ ...cur, ...patch }));
    },
    []
  );

  const paint = useCallback(
    (indices: number[], rgb: Rgb) => {
      setBrush(rgb);
      setDraft((cur) => {
        const c = [...rgb] as Rgb;
        if (cur.effect === 34) {
          const custom = cur.custom.map((x) => [...x] as Rgb);
          for (const i of indices) if (i < KEY_COUNT) custom[i] = [...c] as Rgb;
          return { ...cur, custom };
        }
        const colours = cur.colours.map((x) => [...x] as Rgb);
        for (const i of indices) colours[i] = [...c] as Rgb;
        /* Painting only means something in Direct, so painting puts the board
         * there - the effect selector updates with it. */
        return { ...cur, effect: DIRECT, colours };
      });
    },
    []
  );

  const setColour = useCallback(
    (rgb: Rgb) => {
      const target = selection.length ? selection : KEYS.map((k) => k.led);
      paint(target, rgb);
    },
    [selection, paint]
  );

  /* Selecting a key adopts its colour into the brush, so the picker always
   * shows what the selection is. */
  useEffect(() => {
    if (!selection.length) return;
    const cur = draftRef.current;
    const c = cur.effect === 34 ? cur.custom[selection[0]] : cur.colours[selection[0]];
    if (c) setBrush([...c] as Rgb);
  }, [selection]);

  const apply = useCallback(async () => {
    setBusy(true);
    try {
      await api.apply(draft);
      /* The config packet is always sent.  It used to be skipped when the
       * draft matched what the app believed the board held, which meant a
       * board whose state had drifted (Fn combo, replug, another client)
       * never received the rear-light settings the panel showed. */
      await pushConfig(draft);
      setApplied(clone(draft));
      toast(
        draft.effect === DIRECT ? "Direct frame written to the keyboard" : "Effect applied to the keyboard",
        "ok"
      );
    } catch (err) {
      toast(err instanceof Error ? err.message : "apply failed", "error");
    } finally {
      setBusy(false);
    }
  }, [draft, pushConfig, toast]);

  const revert = useCallback(() => {
    setDraft(clone(applied));
    toast("Reverted to the device state");
  }, [applied, toast]);

  const saveFlash = useCallback(async () => {
    setBusy(true);
    try {
      await api.saveFlash(draft);
      setApplied(clone(draft));
      toast(
        draft.effect === DIRECT
          ? "Settings, remap and macros stored — the Direct frame stays on the host"
          : "Settings, remap, custom picture and macros stored in the board",
        "ok"
      );
    } catch (err) {
      toast(err instanceof Error ? err.message : "could not save", "error");
    } finally {
      setBusy(false);
    }
  }, [draft, toast]);

  const setRemap = useCallback(
    async (index: number, usage: number) => {
      try {
        const r = await api.remap(index, usage);
        setDraft((cur) => ({ ...cur, remap: [...r.remap] }));
        setApplied((cur) => ({ ...cur, remap: [...r.remap] }));
      } catch (err) {
        toast(err instanceof Error ? err.message : "remap failed", "error");
      }
    },
    [toast]
  );

  const remapMany = useCallback(
    async (indices: number[], usage: number) => {
      if (!indices.length) return;
      try {
        const next = [...draftRef.current.remap];
        for (const i of indices) if (i >= 0 && i < next.length) next[i] = usage;
        const r = await api.remapAll(next);
        setDraft((cur) => ({ ...cur, remap: [...r.remap] }));
        setApplied((cur) => ({ ...cur, remap: [...r.remap] }));
        toast(
          `${indices.length} key${indices.length === 1 ? "" : "s"} set to ${usage === 0xff ? "factory" : "a new usage"}`,
          "ok"
        );
      } catch (err) {
        toast(err instanceof Error ? err.message : "remap failed", "error");
      }
    },
    [toast]
  );

  const applyConfig = useCallback(async (): Promise<boolean> => {
    setBusy(true);
    try {
      await pushConfig(draftRef.current);
      toast("Configuration written to the keyboard", "ok");
      return true;
    } catch (err) {
      toast(err instanceof Error ? err.message : "config write failed", "error");
      return false;
    } finally {
      setBusy(false);
    }
  }, [pushConfig, toast]);

  const resetRemap = useCallback(async () => {
    try {
      const r = await api.remapReset();
      setDraft((cur) => ({ ...cur, remap: [...r.remap] }));
      setApplied((cur) => ({ ...cur, remap: [...r.remap] }));
      toast("Keymap reset to the firmware defaults", "ok");
    } catch (err) {
      toast(err instanceof Error ? err.message : "reset failed", "error");
    }
  }, [toast]);

  const saveMacro = useCallback(
    async (slot: number, key: number, steps: MacroStep[]): Promise<boolean> => {
      try {
        const r = await api.macro(slot, key, steps);
        const macros: Macro[] = r.macros.map((m) => ({ ...m, steps: [...m.steps] }));
        setDraft((cur) => ({ ...cur, macros }));
        setApplied((cur) => ({ ...cur, macros }));
        toast(`Macro ${slot + 1} written · flash save queued`, "ok");
        return true;
      } catch (err) {
        toast(err instanceof Error ? err.message : "macro save failed", "error");
        return false;
      }
    },
    [toast]
  );

  const enterBootloader = useCallback(async () => {
    try {
      await api.bootloader();
      toast("Keyboard restarted", "ok");
      window.setTimeout(() => void refresh(), 1800);
    } catch (err) {
      toast(err instanceof Error ? err.message : "restart failed", "error");
    }
  }, [toast, refresh]);

  const bootPreview = useCallback(async () => {
    try {
      await api.bootPreview();
      toast("Start-up animation replayed on the keyboard", "ok");
    } catch (err) {
      toast(err instanceof Error ? err.message : "preview failed", "error");
    }
  }, [toast]);

  /* Flashing is a job on the service: it waits for the ROM bootloader, then
   * writes the image.  The UI just reports progress, because the board has to
   * be replugged before the bootloader can answer. */
  const [flashJob, setFlashJob] = useState<FlashJob>({
    state: "idle",
    message: "",
    output: "",
    ok: null,
    attempts: 0,
    waited: 0,
  });

  const flash = useCallback(async () => {
    setBusy(true);
    try {
      let job = await api.flashStart();
      setFlashJob(job);
      const started = Date.now();
      while (job.state === "waiting" || job.state === "writing") {
        if (Date.now() - started > 200_000) throw new Error("the flash job timed out");
        await new Promise((r) => window.setTimeout(r, 700));
        job = await api.flashStatus();
        setFlashJob(job);
      }
      if (job.ok) toast(job.message || "firmware written", "ok");
      else toast(job.message || "flashing failed", "error");
      await refresh();
    } catch (err) {
      toast(err instanceof Error ? err.message : "flash failed", "error");
    } finally {
      setBusy(false);
    }
  }, [toast, refresh]);

  const saveProfile = useCallback(
    async (name: string): Promise<boolean> => {
      try {
        /* Keymap and macros are not part of Apply, so they are not part of a
         * profile either: loading one used to stage a remap the Apply button
         * could not write, leaving the Keymap page showing a map the board
         * did not hold. */
        const { remap: _remap, macros: _macros, ...lighting } = draft;
        await api.saveProfile({ name, lighting, keyset: selection });
        await loadProfiles();
        toast(`Profile "${name}" saved`, "ok");
        return true;
      } catch (err) {
        toast(err instanceof Error ? err.message : "could not save profile", "error");
        return false;
      }
    },
    [draft, selection, loadProfiles, toast]
  );

  const loadProfile = useCallback(
    async (name: string) => {
      const local = profiles.find((p) => p.name === name);
      if (!local) return;
      /* Whatever the stored JSON says about the keymap and macros, those live
       * on the board and are only changed from their own pages.  Keep the
       * applied values so the UI cannot claim a remap that was never sent. */
      setDraft({
        ...fromServer(local.lighting),
        remap: [...applied.remap],
        macros: applied.macros.map((m) => ({ ...m, steps: m.steps.map((s) => ({ ...s })) })),
      });
      if (local.keyset) setSelection([...local.keyset]);
      toast(`Profile "${name}" loaded - apply to write it to the board`);
    },
    [profiles, applied, toast]
  );

  const deleteProfile = useCallback(
    async (name: string) => {
      try {
        await api.deleteProfile(name);
        await loadProfiles();
        toast(`Profile "${name}" deleted`);
      } catch (err) {
        toast(err instanceof Error ? err.message : "could not delete profile", "error");
      }
    },
    [loadProfiles, toast]
  );

  const saveKeyset = useCallback(
    async (name: string, indices: number[]): Promise<boolean> => {
      try {
        await api.saveKeyset(name, indices);
        await loadProfiles();
        toast(`Keyset "${name}" saved`, "ok");
        return true;
      } catch (err) {
        toast(err instanceof Error ? err.message : "could not save keyset", "error");
        return false;
      }
    },
    [loadProfiles, toast]
  );

  const deleteKeyset = useCallback(
    async (name: string) => {
      try {
        await api.deleteKeyset(name);
        await loadProfiles();
      } catch (err) {
        toast(err instanceof Error ? err.message : "could not delete keyset", "error");
      }
    },
    [loadProfiles, toast]
  );

  /* keyboard shortcuts */
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const el = e.target as HTMLElement | null;
      if (el && /^(INPUT|TEXTAREA|SELECT)$/.test(el.tagName)) return;
      const mod = e.metaKey || e.ctrlKey;
      if (mod && e.key.toLowerCase() === "a") {
        e.preventDefault();
        setSelection(KEYS.map((k) => k.led));
      } else if (mod && e.key === "Enter") {
        e.preventDefault();
        void apply();
      } else if (e.key === "Escape") {
        setSelection([]);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [apply]);

  const value: Ctx = {
    page,
    setPage,
    status,
    connected: Boolean(status?.device.connected),
    draft,
    applied,
    dirty,
    selection,
    setSelection,
    select,
    selectKey,
    selectAll,
    clearSelection,
    invertSelection,
    setColour,
    paint,
    brush,
    setBrush,
    tool,
    setTool,
    setLighting,
    setEffect,
    setConfig,
    setRemap,
    remapMany,
    resetRemap,
    applyConfig,
    bootPreview,
    saveMacro,
    apply,
    revert,
    refresh,
    saveFlash,
    enterBootloader,
    flash,
    flashJob,
    profiles,
    saveProfile,
    loadProfile,
    deleteProfile,
    keysets,
    saveKeyset,
    deleteKeyset,
    toasts,
    toast,
    busy,
  };

  return <Store.Provider value={value}>{children}</Store.Provider>;
}

export function useStore(): Ctx {
  const ctx = useContext(Store);
  if (!ctx) throw new Error("useStore outside StoreProvider");
  return ctx;
}

/** Clamp helper re-exported for the numeric fields. */
export const clampByte = (v: number) => clamp(Math.round(v), 0, 255);
