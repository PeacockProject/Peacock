/* BuildFlow.jsx — Build an image wizard.
 *
 * The Review step's "Build & flash" CTA (and the advanced-footer fallback)
 * routes into FlashFlow — the integrated build + flash-to-device flow.
 * The legacy build-only Run/BuildDone path is no longer reachable from
 * this wizard; FlashFlow runs the same simulated build script in the
 * background while showing the unlock instructions, so the build is now
 * a sub-step of the flash flow rather than a destination of its own. */
import React from "react";
import { AppShell, PK, Btn, Head, SRow, Field, Seg, ModeChip, useMode, FULL, HEAD } from "./shared.jsx";
import { ListDevices } from "./api.js";
import { FALLBACK_DEVICES, DEFAULT_DEVICE_SUPPORT } from "./devices.js";
import { hasWails } from "./devMock.jsx";
import DevicePickerStep from "./DevicePickerStep.jsx";
import BaseStep from "./BaseStep.jsx";
import DesktopStep from "./DesktopStep.jsx";
import PackagesStep from "./PackagesStep.jsx";
import ReviewStep from "./ReviewStep.jsx";
import FlashFlow from "./FlashFlow.jsx";

/* buildSupportMap projects the per-device support data (returned by
 * the real Wails ListDevices() binding via dev.support) into the
 * { [deviceId]: { feature: state | { state, note } } } shape
 * DevicePickerStep expects. Falls back to DEFAULT_DEVICE_SUPPORT for
 * any device whose backend doesn't include a [support] table — which
 * is the case in dev-mode (Cloudflare preview) and for any port whose
 * device.toml hasn't been populated yet. The conversion folds
 * "<key>_note" siblings into { state, note } pairs so the matrix can
 * render the explanatory text. */
function buildSupportMap(devices) {
  const out = { ...DEFAULT_DEVICE_SUPPORT };
  for (const d of devices || []) {
    if (!d || !d.support) continue;
    const raw = d.support;
    const folded = {};
    for (const k of Object.keys(raw)) {
      if (k.endsWith("_note")) continue;
      const note = raw[`${k}_note`];
      folded[k] = note ? { state: raw[k], note } : raw[k];
    }
    if (raw._note) folded._note = raw._note;
    out[d.id] = folded;
  }
  return out;
}

const DESKTOPS = [
  { id: "none", name: "None", m: "console only" },
  { id: "phosh", name: "Phosh", m: "GTK · GNOME mobile" },
  { id: "plasma-mobile", name: "Plasma", m: "Qt · KDE mobile" },
  { id: "sxmo", name: "Sxmo", m: "suckless · minimal" },
  { id: "gnome", name: "GNOME", m: "adaptive desktop" },
  { id: "weston", name: "Weston", m: "reference wayland" },
];
const DMS = ["none", "sddm", "greetd", "lightdm"];
const BSTEPS = ["Device", "Base", "Desktop", "Packages", "Review", "Build"];

export default function BuildFlow({ onHome, startDevice, appClass }) {
  const [devices, setDevices] = React.useState(FALLBACK_DEVICES);
  /* In the real GUI (Wails runtime present), silently showing the sample
   * devices when the backend can't deliver a list is misleading — a build
   * against a sample device will fail later. Surface a warning banner in
   * the picker instead. In pure dev mode (no Wails) the samples ARE the
   * point of the preview, so the fallback stays silent there. */
  const [listError, setListError] = React.useState(false);
  React.useEffect(() => {
    let alive = true;
    ListDevices().then(d => {
      if (!alive) return;
      if (Array.isArray(d) && d.length) { setDevices(d); return; }
      if (hasWails()) setListError(true);
    }).catch(() => { if (alive && hasWails()) setListError(true); });
    return () => { alive = false; };
  }, []);

  const init0 = devices.find(d => d.id === startDevice) || null;
  const [step, setStep] = React.useState(init0 ? 1 : 0);
  const [dev, setDev] = React.useState(init0);
  const [flavor, setFlavor] = React.useState("arch");
  const [initSys, setInitSys] = React.useState("openrc");
  const [arch, setArch] = React.useState(init0 ? init0.arch : "aarch64");
  const [buildMode, setBuildMode] = React.useState("qemu-user");
  const [desktop, setDesktop] = React.useState("phosh");
  const [dm, setDm] = React.useState("sddm");
  const [pkgs, setPkgs] = React.useState(["firefox-esr", "mpv"]);
  const [running, setRunning] = React.useState(false);
  const [mdMode, toggleMode] = useMode();

  const pick = (d) => { setDev(d); setArch(d.arch); };
  const go = (n) => { setStep(n); };
  const canNext = step !== 0 || !!dev;

  const status = (
    <React.Fragment>
      <span className="pd" /><span>{dev ? dev.code : "no device"}</span>
      <span className="sep">·</span><span>{flavor} · {initSys} · {arch}</span>
      <span className="r"><span className="crumb">Build</span><span className="sep">·</span><span>step {Math.min(step + 1, 6)} / 6</span></span>
    </React.Fragment>
  );

  // ----- FLASH FLOW (build + flash-to-device, the new default exit) -----
  if (running) {
    return <FlashFlow
      dev={dev} flavor={flavor} initSys={initSys} desktop={desktop}
      dm={dm} pkgs={pkgs} arch={arch} buildMode={buildMode}
      onHome={onHome} appClass={appClass} />;
  }

  // ----- WIZARD -----
  const basicReview = step === 4 && mdMode !== "advanced";
  const Footer = (
    <div className="mfoot">
      <Btn variant="subtle" onClick={() => (step === 0 ? onHome() : go(step - 1))}>{step === 0 ? "‹ Home" : "‹ Back"}</Btn>
      <div className="sp" />
      <span className="hint">{step === 4 ? (basicReview ? "tap Build & flash above" : "ready to build") : `${BSTEPS[step]} · ${step + 1}/6`}</span>
      {step < 4
        ? <Btn variant="primary" ar="›" disabled={!canNext} onClick={() => go(step + 1)}>Continue</Btn>
        : (basicReview ? null : <Btn variant="grad" ar="→" onClick={() => setRunning(true)}>Build &amp; flash</Btn>)}
    </div>
  );

  return (
    <AppShell appClass={appClass} title={<span>Build an image{dev ? <span className="dim"> · {dev.code}</span> : null}</span>} status={status}>
      <div className="wiz">
        <div className="rail">
          <div className="rt">Build · image</div>
          {BSTEPS.map((s, i) => (
            <div key={s} className={"rstep" + (i === step ? " on" : "") + (i < step ? " done" : "")}
              role={i < step ? "button" : undefined} tabIndex={i < step ? 0 : -1}
              onClick={() => i < step && go(i)}
              onKeyDown={e => { if (i < step && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); go(i); } }}>
              <span className="rn">{i < step ? "" : String(i + 1).padStart(2, "0")}</span><span className="rl">{s}</span>
            </div>
          ))}
          <PK src={FULL} className="wk pkfill" style={{ "--pkc": "#201F24" }} />
          <div className="rfoot">~/.local/var/peacock</div>
        </div>
        <div className="main">
          <ModeChip mode={mdMode} onClick={toggleMode} />
          <div key={step} className="mflow">
          {step === 0 && <DevicePickerStep
            devices={devices}
            dev={dev}
            onPick={pick}
            listError={listError}
            supportMap={buildSupportMap(devices)} />}

          {step === 1 && <BaseStep
            mode={mdMode}
            flavor={flavor} setFlavor={setFlavor}
            initSys={initSys} setInitSys={setInitSys}
            arch={arch} setArch={setArch}
            buildMode={buildMode} setBuildMode={setBuildMode} />}

          {step === 2 && <DesktopStep desktop={desktop} setDesktop={setDesktop} dm={dm} setDm={setDm} mode={mdMode} />}

          {step === 3 && <PackagesStep pkgs={pkgs} setPkgs={setPkgs} mode={mdMode} />}

          {step === 4 && <ReviewStep
            mode={mdMode}
            dev={dev} arch={arch} flavor={flavor} initSys={initSys}
            buildMode={buildMode} desktop={desktop} dm={dm} pkgs={pkgs}
            onStart={() => setRunning(true)} />}
          </div>
          {Footer}
        </div>
      </div>
    </AppShell>
  );
}

