import { effectById } from "../lib/effects";
import type { Page } from "../state/store";
import { useStore } from "../state/store";

const ITEMS: { id: Page; label: string; no: string }[] = [
  { id: "lighting", label: "Lighting", no: "01" },
  { id: "effects", label: "Effects", no: "02" },
  { id: "sound", label: "Sound", no: "03" },
  { id: "keymap", label: "Keymap", no: "04" },
  { id: "macros", label: "Macros", no: "05" },
  { id: "profiles", label: "Profiles", no: "06" },
  { id: "advanced", label: "Advanced", no: "07" },
];

export function Nav() {
  const { page, setPage, draft } = useStore();
  return (
    <nav className="nav">
      {ITEMS.map((item) => (
        <button
          key={item.id}
          type="button"
          className="nav__item"
          data-active={page === item.id}
          onClick={() => setPage(item.id)}
        >
          <span className="nav__index">{item.no}</span>
          {item.label}
        </button>
      ))}
      <div className="nav__spacer" />
      <div className="nav__hint">
        <span>
          <span className="kbd">⌘</span>/<span className="kbd">Ctrl</span>{" "}
          <span className="kbd">↵</span> apply
        </span>
        <span style={{ color: "var(--color-faint)" }}>·</span>
        <span>mode {effectById(draft.effect).name.toLowerCase()}</span>
      </div>
    </nav>
  );
}
