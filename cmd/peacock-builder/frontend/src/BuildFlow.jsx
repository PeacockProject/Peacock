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
import BaseStep from "./BaseStep.jsx";
import DesktopStep from "./DesktopStep.jsx";
import PackagesStep from "./PackagesStep.jsx";
import ReviewStep from "./ReviewStep.jsx";
import FlashFlow from "./FlashFlow.jsx";

const DEVICES_FALLBACK = [
  { id: "samsung-jflte", name: "Galaxy S4", code: "samsung-jflte", soc: "msm8960", arch: "armv7h", tag: "stable" },
  { id: "xiaomi-daisy", name: "Redmi 6A", code: "xiaomi-daisy", soc: "msm8953", arch: "aarch64", tag: "stable" },
  { id: "oppo-a16", name: "Oppo A16", code: "oppo-a16", soc: "mt6765", arch: "aarch64", tag: "testing" },
  { id: "pine-pp", name: "PinePhone", code: "pine64-pinephone", soc: "a64", arch: "aarch64", tag: "stable" },
  { id: "fairphone-fp4", name: "Fairphone 4", code: "fairphone-fp4", soc: "sm7225", arch: "aarch64", tag: "testing" },
  { id: "generic-x86", name: "x86 PC", code: "generic-x86_64", soc: "qemu / uefi", arch: "x86_64", tag: "stable" },
];

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
  const [devices, setDevices] = React.useState(DEVICES_FALLBACK);
  React.useEffect(() => {
    let alive = true;
    ListDevices().then(d => {
      if (alive && Array.isArray(d) && d.length) setDevices(d);
    }).catch(() => { /* keep fallback */ });
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
  const [bdone, setBdone] = React.useState(false);
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
              onClick={() => i < step && go(i)}>
              <span className="rn">{i < step ? "" : String(i + 1).padStart(2, "0")}</span><span className="rl">{s}</span>
            </div>
          ))}
          <PK src={FULL} className="wk pkfill" style={{ "--pkc": "#201F24" }} />
          <div className="rfoot">~/.local/var/peacock</div>
        </div>
        <div className="main">
          <ModeChip mode={mdMode} onClick={toggleMode} />
          <div key={step} className="mflow">
          {step === 0 && <React.Fragment>
            <Head c="STEP 01 / 06 · TARGET" t="Choose a device" s="Pick the device this image will be flashed to. Architecture and bootloader are set from its profile." />
            <div className="mbody fade"><div className="tiles">
              {devices.map(d => (
                <div key={d.id} className={"tile" + (dev && dev.id === d.id ? " on" : "")} onClick={() => pick(d)}>
                  <div className="check">✓</div><div className="tg">{d.tag}</div>
                  <div className="tn">{d.name}</div>
                  <div className="tm">{d.code}<br />{d.soc} · {d.arch}</div>
                </div>
              ))}
            </div></div>
          </React.Fragment>}

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

