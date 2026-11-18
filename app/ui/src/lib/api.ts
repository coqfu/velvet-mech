import { wireMode } from "./effects";
import type { AudioState, FlashJob, Lighting, Macro, Profile, Status } from "./types";

const BASE = "";

async function call<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(BASE + path, {
    headers: { "Content-Type": "application/json" },
    ...init,
  });
  const text = await res.text();
  let body: unknown = null;
  try {
    body = text ? JSON.parse(text) : null;
  } catch {
    body = text;
  }
  if (!res.ok) {
    const msg =
      body && typeof body === "object" && "error" in body
        ? String((body as { error: unknown }).error)
        : `request failed (${res.status})`;
    throw new Error(msg);
  }
  return body as T;
}

/** Only the fields the firmware consumes, so nothing stale is sent. */
function payload(lighting: Lighting, custom = false) {
  return {
    effect: lighting.effect,
    mode: wireMode(lighting.effect),
    hue: lighting.hue,
    sat: lighting.sat,
    val: lighting.val,
    speed: lighting.speed,
    colours: lighting.colours,
    backOn: lighting.backOn,
    backLevel: lighting.backLevel,
    backRgb: lighting.backRgb,
    cal: lighting.cal,
    debounce: lighting.debounce,
    gaming: lighting.gaming,
    ...(custom ? { custom: lighting.custom } : {}),
  };
}

export const api = {
  status: () => call<Status>("/api/status"),
  diag: () => call<Status["diag"]>("/api/diag"),
  connection: (open: boolean) =>
    call<{ ok: boolean; connected: boolean }>("/api/connection", {
      method: "POST",
      body: JSON.stringify({ open }),
    }),
  apply: (lighting: Lighting) =>
    call<{ ok: boolean }>("/api/apply", {
      method: "POST",
      body: JSON.stringify({
        lighting: payload(lighting, lighting.effect === 34),
        custom: lighting.effect === 34 ? lighting.custom : undefined,
      }),
    }),
  saveFlash: (lighting: Lighting) =>
    call<{ ok: boolean }>("/api/save", {
      method: "POST",
      body: JSON.stringify({
        lighting: payload(lighting, true),
        custom: lighting.custom,
      }),
    }),
  config: (
    patch: Partial<
      Pick<
        Lighting,
        | "backOn"
        | "backLevel"
        | "backRgb"
        | "cal"
        | "debounce"
        | "gaming"
        | "bootMode"
        | "bootPace"
        | "backMode"
      >
    >
  ) =>
    call<{ ok: boolean }>("/api/config", {
      method: "POST",
      body: JSON.stringify(patch),
    }),
  bootPreview: () =>
    call<{ ok: boolean }>("/api/boot-preview", { method: "POST", body: "{}" }),
  audio: () => call<AudioState>("/api/audio"),
  setAudio: (patch: Partial<AudioState["params"]> & { enabled?: boolean; source?: string }) =>
    call<AudioState>("/api/audio", { method: "POST", body: JSON.stringify(patch) }),
  remap: (index: number, usage: number) =>
    call<{ ok: boolean; remap: number[] }>("/api/remap", {
      method: "POST",
      body: JSON.stringify({ index, usage }),
    }),
  remapAll: (remap: number[]) =>
    call<{ ok: boolean; remap: number[] }>("/api/remap", {
      method: "POST",
      body: JSON.stringify({ remap }),
    }),
  remapReset: () =>
    call<{ ok: boolean; remap: number[] }>("/api/remap/reset", { method: "POST", body: "{}" }),
  macro: (slot: number, key: number, steps: Macro["steps"]) =>
    call<{ ok: boolean; macros: Macro[] }>("/api/macros", {
      method: "POST",
      body: JSON.stringify({ slot, key, steps }),
    }),
  bootloader: () =>
    call<{ ok: boolean; restarted: boolean; isp: boolean }>("/api/bootloader", {
      method: "POST",
      body: "{}",
    }),
  flashStart: () => call<FlashJob>("/api/flash", { method: "POST", body: "{}" }),
  flashStatus: () => call<FlashJob>("/api/flash"),
  profiles: () => call<{ profiles: Profile[] }>("/api/profiles"),
  saveProfile: (profile: { name: string; lighting: Omit<Lighting, "remap" | "macros">; keyset?: number[] }) =>
    call<{ ok: boolean }>("/api/profiles", {
      method: "POST",
      body: JSON.stringify(profile),
    }),
  deleteProfile: (name: string) =>
    call<{ ok: boolean }>("/api/profiles/delete", {
      method: "POST",
      body: JSON.stringify({ name }),
    }),
  keysets: () => call<{ keysets: Record<string, number[]> }>("/api/keysets"),
  saveKeyset: (name: string, indices: number[]) =>
    call<{ ok: boolean }>("/api/keysets", {
      method: "POST",
      body: JSON.stringify({ name, indices }),
    }),
  deleteKeyset: (name: string) =>
    call<{ ok: boolean }>("/api/keysets/delete", {
      method: "POST",
      body: JSON.stringify({ name }),
    }),
};
