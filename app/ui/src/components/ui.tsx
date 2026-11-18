import type { CSSProperties, ReactNode } from "react";

/* --------------------------------------------------------------- section */
export function Section({
  no,
  title,
  aside,
  children,
}: {
  no?: string;
  title: string;
  aside?: ReactNode;
  children: ReactNode;
}) {
  return (
    <section className="section">
      <div className="section__head">
        <div className="section__title">
          {no && <span className="section__no">{no}</span>}
          <span className="section__name">{title}</span>
        </div>
        {aside && <span className="section__aside">{aside}</span>}
      </div>
      {children}
    </section>
  );
}

/* ----------------------------------------------------------------- field */
export function Field({
  label,
  hint,
  children,
}: {
  label: string;
  hint?: ReactNode;
  children: ReactNode;
}) {
  return (
    <div className="field">
      <span className="field__label">{label}</span>
      <div className="flex items-center gap-2">
        {hint && <span className="field__hint">{hint}</span>}
        {children}
      </div>
    </div>
  );
}

/* ------------------------------------------------------------- segmented */
export function Segmented<T extends string | number>({
  options,
  value,
  onChange,
  disabled,
}: {
  options: { value: T; label: string; disabled?: boolean }[];
  value: T;
  onChange: (v: T) => void;
  disabled?: boolean;
}) {
  return (
    <div className="seg">
      {options.map((o) => (
        <button
          key={String(o.value)}
          type="button"
          className="seg__btn"
          data-active={o.value === value}
          disabled={disabled || o.disabled}
          onClick={() => onChange(o.value)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

/* ---------------------------------------------------------------- slider */
export function Slider({
  value,
  min,
  max,
  step = 1,
  onChange,
  disabled,
  variant,
  label,
}: {
  value: number;
  min: number;
  max: number;
  step?: number;
  onChange: (v: number) => void;
  disabled?: boolean;
  variant?: "hue";
  label?: string;
}) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <input
      className={`slider${variant === "hue" ? " slider--hue" : ""}`}
      type="range"
      aria-label={label}
      min={min}
      max={max}
      step={step}
      value={value}
      disabled={disabled}
      onChange={(e) => onChange(Number(e.target.value))}
      style={{ "--fill": `${pct}%` } as CSSProperties}
    />
  );
}

/* ------------------------------------------------------------- numfield */
export function NumField({
  value,
  min = 0,
  max = 255,
  onChange,
  width = 46,
  disabled,
  suffix,
}: {
  value: number;
  min?: number;
  max?: number;
  onChange: (v: number) => void;
  width?: number;
  disabled?: boolean;
  suffix?: string;
}) {
  return (
    <div className="flex items-center gap-1">
      <input
        className="input input--num"
        type="number"
        value={value}
        min={min}
        max={max}
        disabled={disabled}
        style={{ width }}
        onChange={(e) => {
          const n = Number(e.target.value);
          if (!Number.isNaN(n)) onChange(Math.min(max, Math.max(min, Math.round(n))));
        }}
      />
      {suffix && <span className="field__hint">{suffix}</span>}
    </div>
  );
}

/* ---------------------------------------------------------------- toggle */
export function Toggle({
  on,
  onChange,
  disabled,
  label,
}: {
  on: boolean;
  onChange: (v: boolean) => void;
  disabled?: boolean;
  label: string;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={on}
      aria-label={label}
      className="toggle"
      data-on={on}
      disabled={disabled}
      onClick={() => onChange(!on)}
    />
  );
}

/* ----------------------------------------------------------------- chips */
export function Chips<T extends string>({
  items,
  active,
  onPick,
  onRemove,
}: {
  items: { id: T; label: string }[];
  active?: T | null;
  onPick: (id: T) => void;
  onRemove?: (id: T) => void;
}) {
  return (
    <div className="chips">
      {items.map((item) => (
        <button
          key={item.id}
          type="button"
          className="chip"
          data-active={active === item.id}
          onClick={() => onPick(item.id)}
          onContextMenu={(e) => {
            if (!onRemove) return;
            e.preventDefault();
            onRemove(item.id);
          }}
          title={onRemove ? "Right-click to delete" : undefined}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}

/* ------------------------------------------------------------------ misc */
export function Metric({
  label,
  value,
  unit,
}: {
  label: string;
  value: ReactNode;
  unit?: string;
}) {
  return (
    <div className="metric">
      <div className="metric__label">{label}</div>
      <div className="metric__value">
        {value}
        {unit && <span className="metric__unit">{unit}</span>}
      </div>
    </div>
  );
}

export function KV({ rows }: { rows: [string, ReactNode][] }) {
  return (
    <div className="kv">
      {rows.map(([k, v]) => (
        <div key={k} className="contents">
          <div className="kv__k">{k}</div>
          <div className="kv__v">{v}</div>
        </div>
      ))}
    </div>
  );
}

export function StageHead({
  title,
  subtitle,
  right,
}: {
  title: string;
  subtitle?: string;
  right?: ReactNode;
}) {
  return (
    <div className="stage__head">
      <div>
        <h1 className="title">{title}</h1>
        {subtitle && <p className="subtitle">{subtitle}</p>}
      </div>
      {right && <div className="flex items-center gap-2">{right}</div>}
    </div>
  );
}
