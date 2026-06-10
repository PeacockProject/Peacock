/* InstallFlow.jsx — Calamares-style installer
 *
 * Mounted by two binaries: the installer (live ISO) boots straight into
 * it, and the builder mounts it as a preview behind the Home screen's
 * "Install PeacockOS on this computer" button. `previewNote` defaults
 * OFF so the installer binary (which passes no prop) shows no strip;
 * the builder's App.jsx passes it explicitly. */
import React from "react";
import { AppShell, PK, Btn, Head, SRow, Field, Seg, Toggle, FULL, HEAD } from "./shared.jsx";
import { RunScreen, INSTALL_PHASES, installScript, InstallDone } from "./Run.jsx";
import { APP_VERSION } from "./meta.js";

const LANGS = ["English", "Français", "Deutsch", "Español", "日本語", "Nederlands", "Português", "Italiano", "Polski", "简体中文"];
const REGIONS = [
  { tz: "Europe/Amsterdam", off: "UTC+1" }, { tz: "Europe/London", off: "UTC+0" },
  { tz: "America/New_York", off: "UTC−5" }, { tz: "America/Los_Angeles", off: "UTC−8" },
  { tz: "Asia/Tokyo", off: "UTC+9" }, { tz: "Australia/Sydney", off: "UTC+11" },
];
const KEYBOARDS = [
  { id: "us", name: "English (US)", m: "QWERTY" }, { id: "uk", name: "English (UK)", m: "QWERTY" },
  { id: "de", name: "German", m: "QWERTZ" }, { id: "fr", name: "French", m: "AZERTY" },
  { id: "nl", name: "Dutch", m: "QWERTY" }, { id: "es", name: "Spanish", m: "QWERTY" },
];
const DISKS = [
  { name: "Internal · eMMC", node: "mmcblk0", meta: "samsung-jflte", cap: "16 GB" },
  { name: "microSD", node: "mmcblk1", meta: "removable", cap: "64 GB" },
  { name: "USB-OTG", node: "sda", meta: "SanDisk Ultra", cap: "32 GB" },
];
const ISTEPS = ["Welcome", "Location", "Keyboard", "Disk", "Account", "Summary", "Install"];

function slug(s) { return (s || "").toLowerCase().replace(/[^a-z0-9]+/g, "").slice(0, 16); }

const Lbl = ({ k, warn, children }) => (
  <div className="lblrow"><span className="lk">{k}{warn ? <small style={{ color: "#C2553B" }}>{warn}</small> : null}</span>{children}</div>
);

export default function InstallFlow({ onHome, appClass, previewNote = false }) {
  const [step, setStep] = React.useState(0);
  const [lang, setLang] = React.useState("English");
  const [region, setRegion] = React.useState(REGIONS[0]);
  const [kb, setKb] = React.useState(KEYBOARDS[0]);
  const [disk, setDisk] = React.useState(null);
  const [partMode, setPartMode] = React.useState("erase");
  const [fullname, setFullname] = React.useState("");
  const [username, setUsername] = React.useState("");
  const [hostname, setHostname] = React.useState("peacock");
  const [pw, setPw] = React.useState("");
  const [pw2, setPw2] = React.useState("");
  const [autologin, setAutologin] = React.useState(true);
  const [running, setRunning] = React.useState(false);
  const [idone, setIdone] = React.useState(false);

  const setName = (v) => { setFullname(v); if (!username || username === slug(fullname)) setUsername(slug(v)); };
  const acctOk = username.trim() && pw && pw === pw2;
  const canNext = (step === 3 && !disk) ? false : (step === 4 ? acctOk : true);

  const status = (
    <React.Fragment>
      <span className="pd" /><span>{disk ? "target " + disk.node : "live session"}</span>
      <span className="sep">·</span><span>{lang} · {kb.name}</span>
      <span className="r"><span className="crumb">Install</span><span className="sep">·</span><span>step {Math.min(step + 1, 7)} / 7</span></span>
    </React.Fragment>
  );

  if (running || idone) {
    return <AppShell appClass={appClass} title={<span>Install PeacockOS <span className="dim">· {disk.node}</span></span>} status={status}>
      {idone
        ? <InstallDone user={username} onHome={onHome} />
        : <RunScreen script={installScript(disk, username)} title="Installing PeacockOS"
            meta={`target ${disk.node} · ${disk.cap}`} phases={INSTALL_PHASES}
            eventPrefix="install" onDone={() => setIdone(true)}
            onBack={() => setRunning(false)} />}
    </AppShell>;
  }

  const Footer = (
    <div className="mfoot">
      <Btn variant="subtle" onClick={() => (step === 0 ? onHome() : setStep(step - 1))}>{step === 0 ? "‹ Cancel" : "‹ Back"}</Btn>
      <div className="sp" />
      <span className="hint">{step === 4 && !acctOk ? "enter a username & matching password" : `${ISTEPS[step]} · ${step + 1}/7`}</span>
      {step < 5
        ? <Btn variant="primary" ar="›" disabled={!canNext} onClick={() => setStep(step + 1)}>Continue</Btn>
        : <Btn variant="grad" ar="→" onClick={() => setRunning(true)}>Install now</Btn>}
    </div>
  );

  return (
    <AppShell appClass={appClass} title="Install PeacockOS" status={status}>
      <div className="wiz">
        <div className="rail">
          <div className="rt">Install · disk</div>
          {ISTEPS.map((s, i) => (
            <div key={s} className={"rstep" + (i === step ? " on" : "") + (i < step ? " done" : "")}
              role={i < step ? "button" : undefined} tabIndex={i < step ? 0 : -1}
              onClick={() => i < step && setStep(i)}
              onKeyDown={e => { if (i < step && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); setStep(i); } }}>
              <span className="rn">{i < step ? "" : String(i + 1).padStart(2, "0")}</span><span className="rl">{s}</span>
            </div>
          ))}
          <PK src={FULL} className="wk pkfill" style={{ "--pkc": "#201F24" }} />
          <div className="rfoot">PeacockOS {APP_VERSION} · live</div>
        </div>
        <div className="main">
          {previewNote && (
            <div className="ifl-preview" role="note">
              <span className="ifl-preview-dot" aria-hidden="true" />
              You're previewing the installer. The real install runs from a PeacockOS live USB.
            </div>
          )}
          <div key={step} className="mflow">
          {step === 0 && <React.Fragment>
            <Head c="STEP 01 / 07 · WELCOME" t="Welcome to PeacockOS" s="This assistant installs PeacockOS onto your device. Pick a language to begin — you can change everything later." />
            <div className="mbody fade">
              <div className="seg" style={{ gap: 10 }}>
                {LANGS.map(l => <div key={l} className={"sg" + (lang === l ? " on" : "")} style={{ fontFamily: "var(--sans)", fontSize: 14.5, padding: "11px 18px" }} onClick={() => setLang(l)}>{l}</div>)}
              </div>
            </div>
          </React.Fragment>}

          {step === 1 && <React.Fragment>
            <Head c="STEP 02 / 07 · LOCATION" t="Where are you?" s="Sets your timezone and clock. PeacockOS will keep system time in sync." />
            <div className="mbody fade"><div className="rows">
              {REGIONS.map(r => (
                <div key={r.tz} className={"prow" + (region.tz === r.tz ? " on" : "")} onClick={() => setRegion(r)}>
                  <div className="ic"><span className="gi c" /></div>
                  <div><div className="nm">{r.tz.split("/")[1].replace("_", " ")}</div><div className="mt">{r.tz} · {r.off}</div></div>
                  <div className="radio" />
                </div>
              ))}
            </div></div>
          </React.Fragment>}

          {step === 2 && <React.Fragment>
            <Head c="STEP 03 / 07 · KEYBOARD" t="Keyboard layout" s="Choose a layout, then try it in the field below to make sure it feels right." />
            <div className="mbody fade">
              <div className="tiles" style={{ marginBottom: 20 }}>
                {KEYBOARDS.map(k => (
                  <div key={k.id} className={"tile" + (kb.id === k.id ? " on" : "")} onClick={() => setKb(k)}>
                    <div className="check">✓</div><div className="tn">{k.name}</div><div className="tm">{k.m}</div>
                  </div>
                ))}
              </div>
              <div className="lblrow" style={{ maxWidth: 460 }}>
                <span className="lk">Test your keyboard</span>
                <input className="inp" placeholder="Type here to test…" />
              </div>
            </div>
          </React.Fragment>}

          {step === 3 && <React.Fragment>
            <Head c="STEP 04 / 07 · STORAGE" t="Where should it live?" s="Choose a disk to receive PeacockOS, and how to partition it." />
            <div className="mbody fade">
              <div className="rows" style={{ marginBottom: 18 }}>
                {DISKS.map(d => (
                  <div key={d.node} className={"prow" + (disk && disk.node === d.node ? " on" : "")} onClick={() => setDisk(d)}>
                    <div className="ic"><span className="gi" /></div>
                    <div><div className="nm">{d.name}</div><div className="mt">{d.node} · {d.meta}</div></div>
                    <div className="cap">{d.cap}</div><div className="radio" />
                  </div>
                ))}
              </div>
              <Field l="Partitioning"><Seg v={partMode} set={setPartMode} opts={["erase", "manual"]} /></Field>
              {disk && partMode === "erase" && <div className="callout" style={{ borderLeftColor: "#C2553B", marginTop: 14 }}>
                <div className="ct"><b>Heads up</b> — installing will <b>erase everything</b> on {disk.name} ({disk.node}). This can't be undone.</div></div>}
            </div>
          </React.Fragment>}

          {step === 4 && <React.Fragment>
            <Head c="STEP 05 / 07 · ACCOUNT" t="Create your account" s="Your user on the new system. The password is also used for administrator (sudo) access." />
            <div className="mbody fade"><div style={{ maxWidth: 540, display: "flex", flexDirection: "column", gap: 16 }}>
              <Lbl k="Full name"><input className="inp" value={fullname} placeholder="Pavo Cristatus" onChange={e => setName(e.target.value)} /></Lbl>
              <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16 }}>
                <Lbl k="Username"><input className="inp mono" value={username} placeholder="pavo" onChange={e => setUsername(slug(e.target.value))} /></Lbl>
                <Lbl k="Hostname"><input className="inp mono" value={hostname} onChange={e => setHostname(slug(e.target.value))} /></Lbl>
              </div>
              <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16 }}>
                <Lbl k="Password"><input className="inp" type="password" value={pw} placeholder="••••••••" onChange={e => setPw(e.target.value)} /></Lbl>
                <Lbl k="Confirm" warn={pw2 && pw !== pw2 ? "doesn't match" : null}>
                  <input className="inp" type="password" value={pw2} placeholder="••••••••" onChange={e => setPw2(e.target.value)} /></Lbl>
              </div>
              <div style={{ display: "flex", alignItems: "center", gap: 13, marginTop: 2 }}>
                <Toggle on={autologin} onClick={() => setAutologin(!autologin)} />
                <div><div className="lk" style={{ fontSize: 13.5 }}>Log in automatically</div>
                  <div style={{ fontFamily: "var(--mono)", fontSize: 11.5, color: "var(--faint)" }}>skip the password prompt at boot</div></div>
              </div>
            </div></div>
          </React.Fragment>}

          {step === 5 && <React.Fragment>
            <Head c="STEP 06 / 07 · SUMMARY" t="Review & install" s="Confirm the plan. Nothing has been written yet — clicking Install begins the changes." />
            <div className="mbody fade">
              <div className="summary">
                <SRow k="Language" v={lang} /><SRow k="Timezone" v={region.tz} />
                <SRow k="Keyboard" v={kb.name} /><SRow k="Target disk" v={disk.node + " · " + disk.cap} />
                <SRow k="Partitioning" v={partMode === "erase" ? "erase all" : "manual"} /><SRow k="Hostname" v={hostname} />
                <SRow k="User" v={username || "—"} /><SRow k="Autologin" v={autologin ? "yes" : "no"} />
              </div>
              <div className="callout" style={{ borderLeftColor: "#C2553B", marginTop: 8 }}>
                <PK src={HEAD} style={{ width: 28, height: 34, flex: "0 0 28px" }} className="pkfill" />
                <div className="ct">PeacockOS will be installed to <b>{disk.name}</b> ({disk.node}), erasing its contents. The device will be bootable after a restart.</div>
              </div>
            </div>
          </React.Fragment>}
          </div>
          {Footer}
        </div>
      </div>
    </AppShell>
  );
}
