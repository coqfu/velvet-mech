import { useState } from "react";
import { PageFrame } from "../components/PageFrame";
import { Section } from "../components/ui";
import { effectById, effectForWire } from "../lib/effects";
import type { Profile } from "../lib/types";
import { useStore } from "../state/store";

/** Name the effect a stored profile holds without guessing: an older profile
 *  may only carry the OpenRGB wire mode, which is not an effect id. */
function profileEffect(lighting: Profile["lighting"]): string {
  if (typeof lighting.effect === "number") return effectById(lighting.effect).name;
  if (typeof lighting.mode === "number") {
    const mapped = effectForWire(lighting.mode);
    return mapped == null ? `mode ${lighting.mode}` : effectById(mapped).name;
  }
  return "—";
}

export function ProfilesPage() {
  const { profiles, keysets, draft, selection, saveProfile, loadProfile, deleteProfile, deleteKeyset, select } =
    useStore();
  const [name, setName] = useState("");

  return (
    <PageFrame wide>
      <div className="wide-body">
        <div className="wide-inner">
          <div className="page-head">
            <div>
              <h1 className="title">Profiles</h1>
              <p className="subtitle">
                Lighting states and key selections, stored on the host in <span className="mono">profiles.json</span>.
              </p>
            </div>
            <div className="flex items-center gap-2">
              <input
                className="input"
                style={{ width: 200 }}
                placeholder="Profile name…"
                value={name}
                onChange={(e) => setName(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter" && name.trim()) {
                    const entry = name.trim();
                    void saveProfile(entry).then((ok) => {
                      if (ok) setName("");
                    });
                  }
                }}
              />
              <button
                className="btn"
                disabled={!name.trim()}
                onClick={() => {
                  const entry = name.trim();
                  void saveProfile(entry).then((ok) => {
                    if (ok) setName("");
                  });
                }}
              >
                Save current
              </button>
            </div>
          </div>

          <Section
            no="01"
            title="Stored profiles"
            aside={`${profiles.length} total · workspace: ${effectById(draft.effect).name}`}
          >
            {profiles.length === 0 ? (
              <div className="empty">
                <div className="empty__title">No profiles yet</div>
                <div className="empty__body">
                  Configure the lighting, name it above and save. A profile captures the effect, base
                  colour, brightness, speed, every per-key colour and the board settings Apply
                  writes. The keymap and macros live on the board and are not part of a profile.
                </div>
              </div>
            ) : (
              <div className="box">
                <div className="list">
                  {profiles.map((p) => (
                    <div className="list__row" key={p.name}>
                      <div style={{ width: 10, height: 10, background: "var(--color-line-strong)", borderRadius: 1 }} />
                      <div className="list__main">
                        <div className="list__name">{p.name}</div>
                        <div className="list__meta">
                          {profileEffect(p.lighting)} · val{" "}
                          {p.lighting.val ?? "—"} · speed{" "}
                          {p.lighting.speed != null ? 1 + (p.lighting.speed >> 5) : "—"} · {p.saved}
                          {p.keyset?.length ? ` · ${p.keyset.length} keys` : ""}
                        </div>
                      </div>
                      <button className="btn btn--sm" onClick={() => void loadProfile(p.name)}>
                        Load
                      </button>
                      <button
                        className="btn btn--sm btn--quiet btn--danger"
                        onClick={() => void deleteProfile(p.name)}
                      >
                        Delete
                      </button>
                    </div>
                  ))}
                </div>
              </div>
            )}
            <p className="field__hint" style={{ marginTop: 10, lineHeight: 1.6 }}>
              Loading stages a profile in the workspace. Apply writes it to the panel — nothing is
              sent to the keyboard until you do.
            </p>
          </Section>

          <Section no="02" title="Keysets" aside={`${Object.keys(keysets).length} saved`}>
            {Object.keys(keysets).length === 0 ? (
              <p className="field__hint">
                Select keys on the Lighting page and save them as a keyset to reuse across effects.
              </p>
            ) : (
              <div className="box">
                <div className="list">
                  {Object.entries(keysets).map(([k, indices]) => (
                    <div className="list__row" key={k}>
                      <div className="list__main">
                        <div className="list__name">{k}</div>
                        <div className="list__meta">{indices.length} keys</div>
                      </div>
                      <button
                        className="btn btn--sm"
                        onClick={() => {
                          select(indices);
                        }}
                      >
                        Select
                      </button>
                      <button
                        className="btn btn--sm btn--quiet btn--danger"
                        onClick={() => void deleteKeyset(k)}
                      >
                        Delete
                      </button>
                    </div>
                  ))}
                </div>
              </div>
            )}
            <p className="field__hint" style={{ marginTop: 10 }}>
              Selection size: {selection.length} keys.
            </p>
          </Section>
        </div>
      </div>
    </PageFrame>
  );
}
