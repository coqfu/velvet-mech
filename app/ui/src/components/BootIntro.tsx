import { useEffect, useRef, useState } from "react";

/** Shown once per browser session, before the control surface.  It is a
 *  deliberate first frame for KeyForge, not a loading spinner: the app is
 *  already mounted underneath, so nothing is actually waiting on it. */
const SEEN_KEY = "keyforge.intro.seen";
const HOLD_MS = 1180;
const OUT_MS = 620;

const LOG: [string, string, number][] = [
  ["matrix", "63 keys", 240],
  ["usb", "raw hid", 360],
  ["led engine", "online", 480],
  ["control surface", "ready", 600],
];

export function BootIntro() {
  const [show, setShow] = useState(() => {
    if (typeof window === "undefined") return false;
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) return false;
    try {
      return window.sessionStorage.getItem(SEEN_KEY) !== "1";
    } catch {
      return true;
    }
  });
  const [leaving, setLeaving] = useState(false);
  const skipRef = useRef<() => void>(() => {});

  useEffect(() => {
    if (!show) return;
    try {
      window.sessionStorage.setItem(SEEN_KEY, "1");
    } catch {
      /* private mode: still show it once, just do not remember */
    }

    const out = window.setTimeout(() => setLeaving(true), HOLD_MS);
    const done = window.setTimeout(() => setShow(false), HOLD_MS + OUT_MS);
    skipRef.current = () => setShow(false);

    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape" || e.key === " " || e.key === "Enter") {
        e.preventDefault();
        setShow(false);
      }
    };
    window.addEventListener("keydown", onKey);

    return () => {
      window.clearTimeout(out);
      window.clearTimeout(done);
      window.removeEventListener("keydown", onKey);
    };
  }, [show]);

  if (!show) return null;

  return (
    <div
      className={leaving ? "intro intro--out" : "intro"}
      role="presentation"
      aria-hidden="true"
      onPointerDown={() => skipRef.current()}
      title="Click to skip"
    >
      <div className="intro__grid" />
      <div className="intro__scan" />

      <div className="intro__body">
        <div className="intro__brand" aria-label="KeyForge">
          {"KEYFORGE".split("").map((ch, i) => (
            <span key={`${ch}-${i}`} style={{ animationDelay: `${40 + i * 34}ms` }}>
              {ch}
            </span>
          ))}
        </div>
        <div className="intro__rule" />

        <div className="intro__meta">
          <span>VELVET MECH</span>
          <span>64% / CH555</span>
          <span>63 KEYS</span>
        </div>

        <div className="intro__log">
          {LOG.map(([label, value, delay]) => (
            <p key={label} style={{ animationDelay: `${delay}ms` }}>
              <span>{label}</span>
              <i />
              <b>{value}</b>
            </p>
          ))}
        </div>
      </div>

      <div className="intro__foot">
        <span>KEYFORGE CONTROL</span>
        <span className="intro__skip">click to skip</span>
      </div>
    </div>
  );
}
