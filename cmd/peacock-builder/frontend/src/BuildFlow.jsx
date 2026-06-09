/* BuildFlow.jsx — Build an image wizard */
import React from "react";
import { AppShell, PK, Btn, Head, SRow, Field, Seg, ModeChip, useMode, FULL, HEAD } from "./shared.jsx";
import { RunScreen, BUILD_PHASES, buildScript, BuildDone } from "./Run.jsx";
import { ListDevices } from "./api.js";

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
const PKG_SUGGEST = ["firefox-esr", "mpv", "neovim", "foot", "htop", "git", "openssh", "nmap", "calls", "chatty", "gnome-maps", "angelfish"];
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
  const [mode, setMode] = React.useState("qemu-user");
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

  // ----- RUN screen -----
  if (running || bdone) {
    return <AppShell appClass={appClass} title={<span>Build an image <span className="dim">· {dev.code}</span></span>} status={status}>
      {bdone
        ? <BuildDone dev={dev} flavor={flavor} initSys={initSys} desktop={desktop} onHome={onHome} />
        : <RunScreen script={buildScript(dev, desktop)} title="Assembling image"
            meta={`${dev.code} · ${flavor} · ${initSys}`} phases={BUILD_PHASES} onDone={() => setBdone(true)} />}
    </AppShell>;
  }

  // ----- WIZARD -----
  const Footer = (
    <div className="mfoot">
      <Btn variant="subtle" onClick={() => (step === 0 ? onHome() : go(step - 1))}>{step === 0 ? "‹ Home" : "‹ Back"}</Btn>
      <div className="sp" />
      <span className="hint">{step === 4 ? "ready to build" : `${BSTEPS[step]} · ${step + 1}/6`}</span>
      {step < 4
        ? <Btn variant="primary" ar="›" disabled={!canNext} onClick={() => go(step + 1)}>Continue</Btn>
        : <Btn variant="grad" ar="→" onClick={() => setRunning(true)}>Build image</Btn>}
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

          {step === 1 && <React.Fragment>
            <Head c="STEP 02 / 06 · BASE" t="Base flavor & init" s="The base distribution Peacock bootstraps into the rootfs, and how the system boots." />
            <div className="mbody fade">
              <Field l="Distribution" sub="base-distro">
                <Seg v={flavor} set={setFlavor} opts={["arch", "debian", "alpine"]} /></Field>
              <Field l="Init system">
                <Seg v={initSys} set={setInitSys} opts={["systemd", "openrc"]} /></Field>
              <Field l="Architecture" sub="from device">
                <Seg v={arch} set={setArch} opts={["aarch64", "armv7h", "x86_64"]} /></Field>
              <Field l="Build mode" sub="cross-compile">
                <Seg v={mode} set={setMode} opts={["qemu-user", "native", "cross"]} /></Field>
            </div>
          </React.Fragment>}

          {step === 2 && <React.Fragment>
            <Head c="STEP 03 / 06 · USERLAND" t="Desktop & login" s="Choose a graphical environment and the display manager that greets you. Pick None for a headless console image." />
            <div className="mbody fade">
              <div className="tiles" style={{ marginBottom: 22 }}>
                {DESKTOPS.map(d => (
                  <div key={d.id} className={"tile" + (desktop === d.id ? " on" : "")}
                    onClick={() => { setDesktop(d.id); if (d.id === "none") setDm("none"); else if (dm === "none") setDm("sddm"); }}>
                    <div className="check">✓</div>
                    <div className="tn">{d.name}</div><div className="tm">{d.m}</div>
                  </div>
                ))}
              </div>
              <Field l="Display manager" sub={desktop === "none" ? "n/a · headless" : "greeter"}>
                <Seg v={dm} set={setDm} opts={DMS} dis={desktop === "none" ? DMS.filter(x => x !== "none") : []} /></Field>
            </div>
          </React.Fragment>}

          {step === 3 && <React.Fragment>
            <Head c="STEP 04 / 06 · PACKAGES" t="Extra packages" s="Anything beyond the base + desktop set. Type a name and press Enter, or tap a suggestion." />
            <div className="mbody fade"><Packages pkgs={pkgs} setPkgs={setPkgs} /></div>
          </React.Fragment>}

          {step === 4 && <React.Fragment>
            <Head c="STEP 05 / 06 · REVIEW" t="Ready to build" s="Peacock will cross-compile and assemble a flashable image. This can take several minutes." />
            <div className="mbody fade">
              <div className="summary">
                <SRow k="Device" v={dev.code} /><SRow k="Architecture" v={arch} />
                <SRow k="Distribution" v={flavor} /><SRow k="Init system" v={initSys} />
                <SRow k="Build mode" v={mode} /><SRow k="Desktop" v={desktop} />
                <SRow k="Display manager" v={dm} /><SRow k="Extra packages" v={pkgs.length ? pkgs.length + " selected" : "none"} />
              </div>
              <div className="callout"><PK src={HEAD} style={{ width: 30, height: 36, flex: "0 0 30px" }} className="pkgrad" />
                <div className="ct"><b>Output</b> → <span style={{ fontFamily: "var(--mono)", fontSize: 12.5 }}>~/.local/var/peacock/{dev.code}.img</span><br />
                  Estimated size ≈ {desktop === "none" ? "320 MB" : "1.9 GB"} · ~6 ports built locally.</div></div>
            </div>
          </React.Fragment>}
          </div>
          {Footer}
        </div>
      </div>
    </AppShell>
  );
}

function Packages({ pkgs, setPkgs }) {
  const [val, setVal] = React.useState("");
  const add = (p) => { p = p.trim(); if (p && !pkgs.includes(p)) setPkgs([...pkgs, p]); setVal(""); };
  return (
    <div style={{ maxWidth: 620 }}>
      <div className="chipbox">
        {pkgs.map(p => <span key={p} className="chip">{p}<span className="x" onClick={() => setPkgs(pkgs.filter(x => x !== p))}>×</span></span>)}
        <label className="chip add"><input value={val} placeholder={pkgs.length ? "add…" : "package name…"}
          onChange={e => setVal(e.target.value)} onKeyDown={e => e.key === "Enter" && add(val)} /></label>
      </div>
      <div className="suggest">
        {PKG_SUGGEST.map(p => <span key={p} className={"sug" + (pkgs.includes(p) ? " in" : "")} onClick={() => add(p)}>+ {p}</span>)}
      </div>
    </div>
  );
}
