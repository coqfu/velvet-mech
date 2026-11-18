import { useStore } from "../state/store";

export function StatusBar() {
  const { status, connected, draft, selection } = useStore();
  const d = status?.device;
  const diag = status?.diag;

  return (
    <footer className="statusbar">
      <span className="statusbar__item">
        <span className="dot" data-state={connected ? "ok" : "off"} />
        <b>{connected ? "Link established" : "Link down"}</b>
      </span>
      <span>·</span>
      <span>
        {d?.vendorId != null && d?.productId != null
          ? `${d.vendorId.toString(16).toUpperCase().padStart(4, "0")}:${d.productId
              .toString(16)
              .toUpperCase()
              .padStart(4, "0")}`
          : "—:—"}
      </span>
      <span>·</span>
      <span>FW {d?.qmk || "—"}</span>
      <span>·</span>
      <span>PROTO {d?.protocol != null ? d.protocol : "—"}</span>
      <span>·</span>
      <span>{diag ? `${diag.fps} FPS` : "— FPS"}</span>
      <span className="statusbar__spacer" />
      <span>
        SEL <b>{String(selection.length).padStart(2, "0")}</b>
      </span>
      <span>·</span>
      <span>
        {draft.effect === -1 ? "DIRECT" : "EFFECT"} / {String(draft.val).padStart(3, "0")}
      </span>
      <span>·</span>
      <span>63 KEYS · 69 LEDS</span>
      <span>·</span>
      <span style={{ color: "var(--color-mute)" }}>KEYFORGE 1.0</span>
    </footer>
  );
}
