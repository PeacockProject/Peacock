/* App.jsx — top-level router
 *
 * The mock app.jsx wrapped this in a live <TweaksPanel> for design-review
 * theme tweaking; that file (tweaks-panel.jsx) ships only with the mock and
 * is intentionally not vendored. We keep the small applyTweaks() helper so
 * the iridescent gradient + serif CSS vars get seeded on load. */
import React from "react";
import { AppShell } from "./shared.jsx";
import Home from "./Home.jsx";
import BuildFlow from "./BuildFlow.jsx";
import InstallFlow from "./InstallFlow.jsx";
import PortsSync from "./PortsSync.jsx";
import { PortsStatus } from "./api.js";
import { APP_VERSION } from "./meta.js";

/* BuildGate — peacock-ports must be present before the build wizard is
 * usable (the device picker, the build itself, all read it). On entering
 * the build view we check PortsStatus(); if absent, show the PortsSync
 * clone screen until it's fetched, then mount BuildFlow. In dev mode the
 * PortsStatus stub reports present=true, so the gate is invisible. */
function BuildGate({ onHome, startDevice, appClass }) {
  // null = checking, true = ready, false = needs sync
  const [ready, setReady] = React.useState(null);
  React.useEffect(() => {
    let alive = true;
    PortsStatus()
      .then((st) => { if (alive) setReady(!!(st && st.present)); })
      .catch(() => { if (alive) setReady(false); });
    return () => { alive = false; };
  }, []);

  if (ready === null) return <div className="viewwrap" />; // brief blank while checking
  if (!ready) {
    return <div className="viewwrap"><PortsSync onReady={() => setReady(true)} onHome={onHome} /></div>;
  }
  return <div className="viewwrap"><BuildFlow key="b" onHome={onHome} startDevice={startDevice} appClass={appClass} /></div>;
}

const TWEAK_DEFAULTS = {
  accent: "iridescent",
  serif: "Instrument Serif",
  peacock: "gradient",
  compact: false,
};

const ACCENTS = {
  iridescent: { stops: ["#23B7AE", "#2E86C8", "#6C63D8"], blue: "#2E86C8", css: "linear-gradient(120deg,#23B7AE,#2E86C8 50%,#6C63D8)" },
  azure:      { stops: ["#46A6E8", "#2E86C8", "#1E6FB0"], blue: "#2E86C8", css: "linear-gradient(120deg,#46A6E8,#2E86C8 50%,#1E6FB0)" },
  ink:        { stops: ["#46434C", "#2C2A32", "#1A1820"], blue: "#2C2A32", css: "linear-gradient(120deg,#46434C,#2C2A32 50%,#1A1820)" },
};

function applyTweaks(t) {
  const root = document.documentElement.style;
  const a = ACCENTS[t.accent] || ACCENTS.iridescent;
  root.setProperty("--irid", a.css);
  root.setProperty("--blue", a.blue);
  const stops = document.querySelectorAll("#irid stop");
  if (stops.length === 3) a.stops.forEach((c, i) => stops[i].setAttribute("stop-color", c));
  root.setProperty("--serif", `'${t.serif}',serif`);
}

applyTweaks(TWEAK_DEFAULTS);

export default function App() {
  const t = TWEAK_DEFAULTS;
  const [view, setView] = React.useState(() => localStorage.getItem("pb-view") || "home");
  const [startDevice, setStartDevice] = React.useState(null);
  React.useEffect(() => { localStorage.setItem("pb-view", view); }, [view]);
  React.useEffect(() => { applyTweaks(t); }, [t.accent, t.serif]);

  const home = () => { setStartDevice(null); setView("home"); };
  const appClass = t.compact ? "compact" : "";

  if (view === "build") {
    return <BuildGate onHome={home} startDevice={startDevice} appClass={appClass} />;
  }
  if (view === "install") {
    /* previewNote: in the BUILDER this flow is a demo — the real install
     * runs from the live-ISO binary, whose App.jsx passes no prop and so
     * gets no strip (the prop defaults off in InstallFlow). */
    return <div className="viewwrap"><InstallFlow key="i" onHome={home} appClass={appClass} previewNote /></div>;
  }

  const status = (
    <React.Fragment>
      <span className="pd" /><span>PeacockOS {APP_VERSION}</span><span className="sep">·</span><span>arch · aarch64</span>
      <span className="r"><span className="live" /><span>live session · 3 devices detected</span></span>
    </React.Fragment>
  );
  return (
    <div className="viewwrap">
      <AppShell title="PeacockBuilder" status={status} appClass={appClass}>
        <Home go={setView} peacock={t.peacock} resume={() => { setStartDevice("samsung-jflte"); setView("build"); }} />
      </AppShell>
    </div>
  );
}
