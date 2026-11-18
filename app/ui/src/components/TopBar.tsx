import { useEffect, useState } from "react";
import { api } from "../lib/api";
import { useStore } from "../state/store";

export function TopBar() {
  const { status, connected, dirty, profiles, loadProfile, refresh } = useStore();
  const [busy, setBusy] = useState(false);
  const [selected, setSelected] = useState("");

  const device = status?.device;
  const released = Boolean(device?.released);

  useEffect(() => {
    if (!profiles.length) setSelected("");
  }, [profiles.length]);

  const toggleConnection = async () => {
    setBusy(true);
    try {
      await api.connection(!connected);
      await refresh();
    } finally {
      setBusy(false);
    }
  };

  return (
    <header className="topbar">
      <div className="brand">
        <span className="brand__mark" />
        <span className="brand__name">KEYFORGE</span>
        <span className="brand__sub">Configuration Terminal</span>
      </div>

      <div style={{ width: 1, height: 16, background: "var(--color-line)" }} />

      <div className="device-chip">
        <span className="dot" data-state={connected ? "ok" : "off"} />
        <span className="device-chip__name">{device?.name || "Velvet Mech"}</span>
        <span className="device-chip__meta">
          {device?.vendorId != null && device?.productId != null
            ? `${device.vendorId.toString(16).toUpperCase().padStart(4, "0")}:${device.productId
                .toString(16)
                .toUpperCase()
                .padStart(4, "0")}`
            : "—:—"}
        </span>
        <span className="device-chip__meta" style={{ color: "var(--color-faint)" }}>
          {connected ? device?.path?.replace("/dev/", "") ?? "—" : "offline"}
        </span>
      </div>

      <div style={{ flex: 1 }} />

      <div className="flex items-center gap-2">
        <span className="micro">Profile</span>
        <select
          className="select"
          value={selected}
          onChange={(e) => {
            const name = e.target.value;
            setSelected(name);
            /* Loading stages a profile, so the menu goes back to describing
             * the workspace.  Leaving the name selected made picking the same
             * profile again a no-op, which read as a broken "revert". */
            if (name) {
              void loadProfile(name).then(() => setSelected(""));
            }
          }}
          style={{ minWidth: 148 }}
        >
          <option value="">{dirty ? "Unsaved state" : "Device state"}</option>
          {profiles.map((p) => (
            <option key={p.name} value={p.name}>
              {p.name}
            </option>
          ))}
        </select>
        {dirty && <span className="dirty-mark" title="Unapplied changes" />}
      </div>

      <div style={{ width: 1, height: 16, background: "var(--color-line)" }} />

      <div className="flex items-center gap-2">
        <span
          className="micro"
          style={{ color: connected ? "var(--color-ok)" : "var(--color-warn)" }}
        >
          {connected ? "USB · Ready" : released ? "Released" : "No device"}
        </span>
        <button className="btn btn--sm btn--quiet" onClick={() => void toggleConnection()} disabled={busy}>
          {connected ? "Release" : "Reconnect"}
        </button>
      </div>
    </header>
  );
}
