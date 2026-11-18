import { useMemo, useState } from "react";
import { Keyboard } from "../components/Keyboard";
import { PageFrame, Panel, Stage, StageBody, StageFoot } from "../components/PageFrame";
import { KV, Section, Segmented, StageHead } from "../components/ui";
import { KEYS } from "../lib/keyboard-data";
import { FACTORY_USAGE, USAGE, USAGE_GROUPS, usageName } from "../lib/usages";
import { useStore } from "../state/store";

/** One row of the "changed keys" table. */
type Change = { led: number; id: string; from: number; to: number };

export function KeymapPage() {
  const { draft, selection, selectAll, clearSelection, setRemap, remapMany, resetRemap, toast } = useStore();
  const [layer, setLayer] = useState<"base" | "fn">("base");
  const [query, setQuery] = useState("");

  /* Fn is the layer key: keyboard_report() skips it, so an override stored
   * for it would never be sent.  Keep it selectable for inspection, but do
   * not offer it as a remap target. */
  const FN_LED = KEYS.find((k) => k.id === "FN")?.led ?? -1;
  const remappable = useMemo(() => selection.filter((i) => i !== FN_LED), [selection, FN_LED]);

  const selectedKeys = useMemo(
    () => KEYS.filter((k) => selection.includes(k.led)),
    [selection]
  );
  const one = selectedKeys.length === 1 ? selectedKeys[0] : undefined;

  const current = (led: number) => draft.remap[led] ?? USAGE.FACTORY;
  const isChanged = (led: number) => led !== FN_LED && current(led) !== USAGE.FACTORY;

  const changes: Change[] = useMemo(
    () =>
      KEYS.filter((k) => isChanged(k.led)).map((k) => ({
        led: k.led,
        id: k.id,
        from: FACTORY_USAGE[k.led] ?? 0,
        to: current(k.led),
      })),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [draft.remap]
  );

  const groups = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return USAGE_GROUPS;
    return USAGE_GROUPS.map((g) => ({
      label: g.label,
      items: g.items.filter(
        (u) => usageName(u).toLowerCase().includes(q) || `0x${u.toString(16)}`.includes(q)
      ),
    })).filter((g) => g.items.length > 0);
  }, [query]);

  const assign = async (usage: number) => {
    if (!selection.length) {
      toast("Select a key first", "warn");
      return;
    }
    if (!remappable.length) {
      toast("Fn is the layer key and cannot be remapped", "warn");
      return;
    }
    if (remappable.length !== selection.length) {
      toast("Fn skipped · it stays the layer key", "warn");
    }
    await remapMany(remappable, usage);
  };

  return (
    <PageFrame>
      <Stage>
        <StageHead
          title="Keymap"
          subtitle="Remap base-layer keys to the usages below. Writes go straight to the board and survive a power cycle."
          right={
            <div style={{ minWidth: 176 }}>
              <Segmented
                options={[
                  { value: "base", label: "Base" },
                  { value: "fn", label: "Fn layer" },
                ]}
                value={layer}
                onChange={setLayer}
              />
            </div>
          }
        />
        <StageBody>
          <Keyboard layer={layer} readOnly={false} mono strip={false} />
        </StageBody>
        <StageFoot>
          <div className="flex items-center gap-3">
            <span className="micro">Selection</span>
            <span className="value">
              {selection.length ? String(selection.length).padStart(2, "0") : "00"}
              <span className="value--dim"> keys</span>
            </span>
            <button className="chip" onClick={() => selectAll()}>
              All
            </button>
            <button className="chip" onClick={() => clearSelection()}>
              None
            </button>
          </div>
          <span className="field__hint">
            The Fn layer is compiled in; only the base layer is remappable, and the Fn key itself
            stays the layer key.
          </span>
        </StageFoot>
      </Stage>

      <Panel>
        <Section no="01" title="Selection" aside={one ? one.id : `${selectedKeys.length} keys`}>
          {one && one.id === "FN" ? (
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              Fn is the layer key. The firmware reads it before the report is built and never sends
              it as a character, so it has no remappable usage; hold it to reach the compiled Fn
              layer.
            </p>
          ) : one ? (
            <KV
              rows={[
                ["Key", one.id],
                ["Matrix", `col ${one.col} · row ${one.row}`],
                ["LED", String(one.led)],
                ["Factory", usageName(FACTORY_USAGE[one.led] ?? 0)],
                [
                  "Assigned",
                  <span key="a" style={{ color: isChanged(one.led) ? "var(--color-warn)" : undefined }}>
                    {usageName(current(one.led))}
                  </span>,
                ],
              ]}
            />
          ) : selectedKeys.length > 1 ? (
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              {selectedKeys.map((k) => k.id).join(" · ")} — the assignment below applies to all of
              them{remappable.length !== selection.length ? ", except Fn, which stays the layer key" : ""}.
            </p>
          ) : (
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              Click a key on the board to inspect it. Shift-click adds, ⌘/Ctrl-click toggles,
              drag selects a range.
            </p>
          )}
        </Section>

        <Section no="02" title="Assign" aside={selection.length ? "to selection" : "no target"}>
          <div className="flex items-center gap-2" style={{ marginBottom: 10 }}>
            <input
              className="input"
              placeholder="Search usage…"
              value={query}
              onChange={(e) => setQuery(e.target.value)}
            />
            <button
              className="chip"
              disabled={!remappable.length}
              onClick={() => void assign(USAGE.FACTORY)}
            >
              Factory
            </button>
            <button
              className="chip"
              disabled={!remappable.length}
              onClick={() => void assign(USAGE.NONE)}
            >
              Disable
            </button>
          </div>

          <div className="usage-list">
            {groups.map((g) => (
              <div key={g.label} className="usage-group">
                <div className="usage-group__name">{g.label}</div>
                <div className="usage-grid">
                  {g.items.map((u) => (
                    <button
                      key={u}
                      className="chip"
                      disabled={!remappable.length}
                      data-active={one ? current(one.led) === u : false}
                      onClick={() => void assign(u)}
                      title={`HID usage 0x${u.toString(16).toUpperCase().padStart(2, "0")}`}
                    >
                      {usageName(u)}
                    </button>
                  ))}
                </div>
              </div>
            ))}
            {groups.length === 0 && (
              <p className="field__hint">No usage matches “{query}”.</p>
            )}
          </div>
        </Section>

        <Section no="03" title="Changed" aside={`${changes.length} keys`}>
          {changes.length === 0 ? (
            <p className="field__hint field__hint--prose" style={{ lineHeight: 1.6 }}>
              Every key is on its factory usage. Assignments appear here and persist in the board's
              flash.
            </p>
          ) : (
            <div className="remap-list">
              {changes.map((c) => (
                <div key={c.led} className="remap-row">
                  <span className="remap-row__key">{c.id}</span>
                  <span className="remap-row__from">{usageName(c.from)}</span>
                  <span className="remap-row__arrow">→</span>
                  <span className="remap-row__to">{usageName(c.to)}</span>
                  <button
                    className="chip"
                    onClick={() => void setRemap(c.led, USAGE.FACTORY)}
                    title="Restore the factory usage"
                  >
                    Restore
                  </button>
                </div>
              ))}
            </div>
          )}
          <button
            className="btn btn--sm btn--quiet"
            style={{ marginTop: 12 }}
            disabled={changes.length === 0}
            onClick={() => void resetRemap()}
          >
            Reset all keys
          </button>
        </Section>
      </Panel>
    </PageFrame>
  );
}
