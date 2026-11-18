import { EFFECTS, type EffectGroup } from "../lib/effects";

export function EffectList({
  value,
  onChange,
  group = "all",
}: {
  value: number;
  onChange: (id: number) => void;
  /** Restrict the list to one group.  The Effects page has group chips, so
   *  they have to actually filter rather than only relabel the count. */
  group?: EffectGroup | "all";
}) {
  const items = group === "all" ? EFFECTS : EFFECTS.filter((fx) => fx.group === group);
  return (
    <div className="fx-list">
      {items.map((fx) => (
        <button
          key={fx.id}
          type="button"
          className="fx-item"
          data-active={value === fx.id}
          onClick={() => onChange(fx.id)}
          title={fx.desc}
        >
          <span className="fx-item__no">{fx.code}</span>
          <span style={{ flex: 1, minWidth: 0, textAlign: "left" }}>
            <div className="fx-item__name">{fx.name}</div>
            <div className="fx-item__desc">{fx.desc}</div>
          </span>
          <span className="fx-item__id">{fx.id < 0 ? "—" : fx.id.toString(16).padStart(2, "0").toUpperCase()}</span>
        </button>
      ))}
    </div>
  );
}
