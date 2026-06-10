/* Run.jsx — shared live-progress screen + done screens.
 *
 * Phase 4 (in-process pipeline call): when the Wails runtime is
 * present, the Run screen subscribes to real backend events:
 *
 *   "build:log"   → append a log chunk to the scroll buffer
 *   "build:phase" → update the phase pill / progress bar
 *   "build:done"  → image path payload, terminal-success state
 *   "build:error" → error string payload, terminal-failure state
 *
 *   (the installer side uses the same shapes with an "install:" prefix
 *    — peacock-installer's install_runner.go already emits them.)
 *
 * Dev-mode fallback: when window.go is null (pure `npm run dev` outside
 * Wails — the Cloudflare Workers preview friends are using) we serve
 * the simulated buildScript / installScript from ./devMock.js so the
 * wizard still demos end-to-end. The simulated arrays are intentionally
 * not bundled into the Wails production build path; they're imported
 * inside the dev-only branch and tree-shaken when unused.
 *
 * Compatibility: BUILD_PHASES / INSTALL_PHASES / buildScript /
 * installScript are re-exported here so other files (FlashFlow.jsx,
 * InstallFlow.jsx) that import them from "./Run.jsx" keep working
 * unchanged. They're now sourced from ./devMock.js. */

import React from "react";
import { PK, Btn, FULL } from "./shared.jsx";
import {
  BUILD_PHASES,
  INSTALL_PHASES,
  buildScript,
  installScript,
  hasWails,
} from "./devMock.jsx";

export { BUILD_PHASES, INSTALL_PHASES, buildScript, installScript };

/* useWailsScript — subscribe to a "<prefix>:log" / "<prefix>:phase" /
 * "<prefix>:done" / "<prefix>:error" event stream and yield the same
 * { lines, prog, phase, done, error } shape RunScreen consumes when
 * driving the simulated path.
 *
 * eventPrefix is "build" or "install". When hasWails() is false this
 * hook returns null so callers can fall back to the script-driven
 * timer in RunScreen. */
function useWailsScript(eventPrefix, phases) {
  const [lines, setLines] = React.useState([]);
  const [prog, setProg] = React.useState(0);
  const [phase, setPhase] = React.useState(phases[0].label);
  const [done, setDone] = React.useState(false);
  const [errorMsg, setErrorMsg] = React.useState(null);

  React.useEffect(() => {
    if (!hasWails() || !window.runtime || typeof window.runtime.EventsOn !== "function") return;

    const unsubs = [];
    const subscribe = (name, cb) => {
      const off = window.runtime.EventsOn(name, cb);
      if (typeof off === "function") unsubs.push(off);
    };

    let nextT = "·";
    const stamp = () => {
      const d = new Date();
      const hh = String(d.getHours()).padStart(2, "0");
      const mm = String(d.getMinutes()).padStart(2, "0");
      const ss = String(d.getSeconds()).padStart(2, "0");
      return `${hh}:${mm}:${ss}`;
    };

    subscribe(`${eventPrefix}:log`, (chunk) => {
      // The pipeline writes chunks separated by newlines; split so the
      // log scroll buffer doesn't render multi-line strings as one row.
      const text = typeof chunk === "string" ? chunk : String(chunk);
      const split = text.split("\n").filter(s => s.length > 0);
      if (split.length === 0) return;
      setLines(prev => {
        const t = stamp();
        const next = prev.slice();
        for (const ln of split) {
          next.push({ t, prog: prog, node: <span>{ln}</span> });
        }
        return next;
      });
    });

    subscribe(`${eventPrefix}:phase`, (payload) => {
      // peacock-installer emits a structured Progress object; the
      // build side currently only ticks log lines but will gain a
      // phase emitter alongside this skill. Defensive parsing handles
      // both shapes.
      let p = payload;
      if (typeof payload === "string") {
        try { p = JSON.parse(payload); } catch (_e) { p = { phase: payload }; }
      }
      if (p && typeof p === "object") {
        if (typeof p.percent === "number") setProg(p.percent);
        if (typeof p.phase === "string") setPhase(p.phase);
      }
    });

    subscribe(`${eventPrefix}:done`, () => {
      setDone(true);
      setProg(100);
    });

    subscribe(`${eventPrefix}:error`, (payload) => {
      const msg = typeof payload === "string" ? payload : "build failed";
      setErrorMsg(msg);
      setDone(true);
    });

    return () => {
      for (const off of unsubs) {
        try { off(); } catch (_e) { /* noop */ }
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [eventPrefix]);

  if (!hasWails()) return null;
  return { lines, prog, phase, done, errorMsg };
}

/* RunScreen — the shared progress-bar + scroll-buffer view. When
 * driven by a real backend (Wails present), subscribes to the
 * eventPrefix-keyed stream. When in dev-mode (no Wails), falls back to
 * the script-driven simulated path the maintainer's preview deploy
 * still needs.
 *
 * Backward-compatible: existing callers that pass `script` + `phases`
 * keep working — they're treated as the dev-mode source of truth and
 * the live path is opt-in via the `eventPrefix` prop. */
export function RunScreen({ script, title, meta, phases, onDone, onBack, eventPrefix }) {
  const live = useWailsScript(eventPrefix || "build", phases);

  const [n, setN] = React.useState(0);
  const simProg = n > 0 ? script[n - 1].prog : 0;
  React.useEffect(() => {
    if (live) return; // Wails events drive progress, no timer needed.
    if (n >= script.length) { const t = setTimeout(onDone, 900); return () => clearTimeout(t); }
    const t = setTimeout(() => setN(n + 1), n === 0 ? 300 : 300 + Math.random() * 240);
    return () => clearTimeout(t);
  }, [n, live]);

  // Fire onDone once for the live path when the backend emits :done —
  // but NOT on :error (it also flips `done` to stop the spinner; the
  // failure banner below owns that state instead of the success screen).
  React.useEffect(() => {
    if (!live || !live.done || live.errorMsg) return;
    const t = setTimeout(onDone, 600);
    return () => clearTimeout(t);
  }, [live && live.done]);

  const prog = live ? live.prog : simProg;
  const phase = live
    ? live.phase
    : phases.reduce((a, p) => (prog >= p.at ? p.label : a), phases[0].label);
  const lines = live ? live.lines : script.slice(0, n);
  const recent = lines.slice(-5);
  const [showLog, setShowLog] = React.useState(false);
  const stillRunning = live ? !live.done : n < script.length;
  const failed = !!(live && live.errorMsg);
  const failTitle = (eventPrefix || "build") === "install" ? "Install failed" : "Build failed";

  return (
    <div className="rprog">
      <div className="rpl">
        <div className="glow" />
        <div className="meta">{meta}</div>
        {failed ? (
          /* Failure state: the progress readout is replaced by an explicit
           * banner so the user never stares at a stuck bar. The backend's
           * error string is shown verbatim. */
          <div className="rfail" role="alert">
            <div className="rfail-tag">{failTitle.toUpperCase()}</div>
            <h2 className="rfail-h2">{failTitle}</h2>
            <p className="rfail-lead">Something went wrong and we had to stop. The exact error was:</p>
            <pre className="rfail-msg">{live.errorMsg}</pre>
            <div className="rfail-acts">
              <Btn variant="primary" onClick={() => setShowLog(true)}>Show full log</Btn>
              {onBack && <Btn variant="ghost" onClick={onBack}>Back</Btn>}
            </div>
          </div>
        ) : (
          <React.Fragment>
            <div className="bigpct">{Math.round(prog)}<span className="pp">%</span></div>
            <h2>{title}</h2>
            <div className="phase">{prog >= 100 ? "Complete" : phase + "…"}</div>
            <div className="rtrack"><i style={{ width: prog + "%" }} /></div>
            <div className="rsteps">{phases.map((p, i) => {
              const cur = phase === p.label && prog < 100;
              const done = prog > p.at && phase !== p.label || prog >= 100;
              return <span key={i} className={"stp" + (done ? " done" : cur ? " cur" : "")}><span className="d" />{p.label}</span>;
            })}</div>
          </React.Fragment>
        )}
        <button className="loglink" onClick={() => setShowLog(s => !s)}>
          {showLog ? "Hide full log ‹" : "Show full log ›"}
        </button>
      </div>
      <div className={"rpr" + (showLog ? " dark" : "")}>
        <div key={showLog ? "log" : "pk"} className="rprfill">
        {showLog ? (
          <React.Fragment>
            <div className="topfade" />
            <div className="logwrap">
              {lines.map((l, i) => <div key={i} className="ln"><span className="t">{l.t} </span>{l.node}</div>)}
              {stillRunning && <div className="ln"><span className="cur">▍</span></div>}
            </div>
          </React.Fragment>
        ) : (
          <React.Fragment>
            <div className="aura" />
            <PK src={FULL} className="pkprog pkgrad" />
            <div className="rrecent">
              {recent.map((l, i) => <div key={i} className="ln">{l.node}</div>)}
            </div>
          </React.Fragment>
        )}
        </div>
      </div>
    </div>
  );
}

export function BuildDone({ dev, flavor, initSys, desktop, onHome }) {
  const [copied, setCopied] = React.useState(false);
  const path = `~/.local/var/peacock/${dev.code}.img`;
  return (
    <div className="finscr">
      <div className="glow" />
      <PK src={FULL} className="pkd pkgrad" />
      <div className="tag">IMAGE READY · {flavor} · {initSys}</div>
      <h1>Your image is <em>ready.</em></h1>
      <p>A flashable PeacockOS image for the {dev.name} has been assembled. Flash it over fastboot, or write it to an SD card.</p>
      <div className="codebox"><span><span className="pr">$</span> peacock flash --device {dev.code}</span>
        <span className="cp" onClick={() => { setCopied(true); setTimeout(() => setCopied(false), 1400); }}>{copied ? "copied" : "copy"}</span></div>
      <div className="acts">
        <Btn variant="grad" ar="→" onClick={onHome}>Flash to device</Btn>
        <Btn variant="ghost" onClick={onHome}>Show in folder</Btn>
        <Btn variant="subtle" onClick={onHome}>Done</Btn>
      </div>
    </div>
  );
}

export function InstallDone({ user, onHome }) {
  return (
    <div className="finscr">
      <div className="glow" />
      <PK src={FULL} className="pkd pkgrad" />
      <div className="tag">INSTALLATION COMPLETE</div>
      <h1>PeacockOS is <em>installed.</em></h1>
      <p>The system is ready on your disk. Remove the live medium and reboot to start using PeacockOS{user ? `, ${user}` : ""}.</p>
      <div className="acts">
        <Btn variant="grad" ar="⟳" onClick={onHome}>Restart now</Btn>
        <Btn variant="ghost" onClick={onHome}>Keep testing live</Btn>
      </div>
    </div>
  );
}
