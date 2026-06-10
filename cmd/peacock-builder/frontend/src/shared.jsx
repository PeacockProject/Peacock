/* shared.jsx — PeacockBuilder shared atoms + shell */
import React from "react";

const PKBASE = import.meta.env.BASE_URL;
export const FULL = PKBASE + "assets/peacock-full.svg";
export const HEAD = PKBASE + "assets/peacock-head.svg";

const __cache = {};
export function PK({ src, className = "", style }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    let alive = true;
    const apply = (t) => {
      if (!alive || !ref.current) return;
      ref.current.innerHTML = t;
      const s = ref.current.querySelector("svg");
      if (s) { s.removeAttribute("width"); s.removeAttribute("height"); }
    };
    if (__cache[src]) apply(__cache[src]);
    else fetch(src).then(r => r.text()).then(t => { __cache[src] = t; apply(t); });
    return () => { alive = false; };
  }, [src]);
  return <div ref={ref} className={"pkw " + className} style={style} />;
}

export const Geo = ({ s }) => <span className={"gi" + (s ? " " + s : "")} />;

/* keyActivate — shared Enter/Space → click adapter so the div-based
 * interactive atoms below behave like real buttons for keyboard users. */
function keyActivate(onClick) {
  return (e) => {
    if (e.key !== "Enter" && e.key !== " ") return;
    e.preventDefault();
    if (onClick) onClick(e);
  };
}

export function Btn({ variant = "ghost", cap, ar, sub, onClick, disabled, children, style }) {
  return (
    <div className={"btn " + variant} role="button" tabIndex={disabled ? -1 : 0}
      aria-disabled={disabled || undefined}
      onClick={disabled ? null : onClick}
      onKeyDown={disabled ? null : keyActivate(onClick)}
      disabled={disabled || undefined} style={style}>
      <span className="lab">{children}{sub ? <small>{sub}</small> : null}</span>
      {ar ? <span className="ar">{ar}</span> : null}
      {cap ? <span className="cap">{cap}</span> : null}
    </div>
  );
}

export const Toggle = ({ on, onClick }) => (
  <div className={"toggle" + (on ? " on" : "")} role="switch" aria-checked={!!on}
    tabIndex={0} onClick={onClick} onKeyDown={keyActivate(onClick)} />
);

/* motion: useLoaded flips true on the frame after mount (capture-safe entrance) */
export function useLoaded() {
  const [on, setOn] = React.useState(false);
  React.useEffect(() => {
    let r2;
    const r1 = requestAnimationFrame(() => { r2 = requestAnimationFrame(() => setOn(true)); });
    return () => { cancelAnimationFrame(r1); if (r2) cancelAnimationFrame(r2); };
  }, []);
  return on;
}

export function Reveal({ children, className = "", style, delay = 0, y = 10, dur = 520 }) {
  const on = useLoaded();
  return (
    <div className={className} style={{
      ...(style || {}),
      opacity: on ? 1 : 0,
      transform: on ? "none" : `translateY(${y}px)`,
      transition: `opacity ${dur}ms cubic-bezier(.2,.7,.2,1) ${delay}ms, transform ${dur}ms cubic-bezier(.2,.7,.2,1) ${delay}ms`,
    }}>{children}</div>
  );
}

export const Head = ({ c, t, s }) => (
  <div className="mhead"><div className="mcount">{c}</div><div className="mtitle">{t}</div><div className="msub">{s}</div></div>
);
export const SRow = ({ k, v }) => <div className="srow"><span className="sk">{k}</span><span className="sv">{v}</span></div>;
export const Field = ({ l, sub, children }) => (
  <div className="field"><div className="fl">{l}{sub ? <small>{sub}</small> : null}</div><div>{children}</div></div>
);
export const Seg = ({ v, set, opts, dis = [] }) => (
  <div className="seg">{opts.map(o => (
    <div key={o} className={"sg" + (v === o ? " on" : "")} data-dis={dis.includes(o) ? "" : undefined} onClick={() => set(o)}>{o}</div>
  ))}</div>
);

const TBC = () => <span className="ctr"><span className="c" /><span className="c" /><span className="c" /></span>;

export function AppShell({ title, status, appClass, children }) {
  return (
    <div className={"app " + (appClass || "")}>
      <div className="tbar">
        <PK src={HEAD} className="mk pkgrad" />
        <span className="ttl">{title}</span>
        <TBC />
      </div>
      <div className="stage">{children}</div>
      <div className="sbar">{status}</div>
    </div>
  );
}

/* ---- basic/advanced mode toggle (persisted, used by wizard steps) ----
 * useMode returns [mode, toggle, setMode]. Persisted to localStorage under
 * "pb:mode". Default "basic" so newcomers get the hand-holding flow.
 * Sibling step components consume `mode` as a prop. */
export function useMode() {
  const [mode, setMode] = React.useState(() => {
    try { return localStorage.getItem("pb:mode") === "advanced" ? "advanced" : "basic"; }
    catch { return "basic"; }
  });
  React.useEffect(() => {
    try { localStorage.setItem("pb:mode", mode); } catch { /* ignore */ }
  }, [mode]);
  const toggle = React.useCallback(() => setMode(m => m === "advanced" ? "basic" : "advanced"), []);
  return [mode, toggle, setMode];
}

/* Small pill chip used in the wizard top-right corner. */
export function ModeChip({ mode, onClick }) {
  const on = mode === "advanced";
  return (
    <div className={"modechip" + (on ? " on" : "")} role="button" tabIndex={0}
      aria-pressed={on} onClick={onClick} onKeyDown={keyActivate(onClick)}
      title={on ? "Showing every option" : "Show every option"}>
      <span className="dot" />Advanced
    </div>
  );
}

/* keyboard shortcut hook */
export function useKeys(map) {
  React.useEffect(() => {
    const h = (e) => {
      if (e.target && /input|textarea/i.test(e.target.tagName)) return;
      const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
      if (map[k]) { e.preventDefault(); map[k](); }
    };
    window.addEventListener("keydown", h);
    return () => window.removeEventListener("keydown", h);
  }, [map]);
}
