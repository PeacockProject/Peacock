/* ReviewStep.jsx — Step 5 of the build wizard ("Ready to build")
 *
 * Basic mode: friendly summary paragraph + collapsible "What's inside" +
 * one big primary CTA. No Linux jargon outside the device codename.
 *
 * Advanced mode: keeps the original 2-column k:v summary + callout with
 * output path + estimated size in monospace. */
import React from "react";
import { Head, SRow, PK, Btn, HEAD, FULL } from "./shared.jsx";

/* Friendly label mapping so the "What's inside" list reads as plain
 * English instead of repo lingo. */
const FRIENDLY_DESKTOP = {
  none: "Console-only (no graphical screen)",
  phosh: "Phosh — the mobile desktop",
  "plasma-mobile": "Plasma Mobile — the KDE mobile desktop",
  sxmo: "Sxmo — a minimal scriptable shell",
  gnome: "GNOME — the adaptive desktop",
  weston: "Weston — a reference Wayland compositor",
};
const FRIENDLY_FLAVOR = {
  arch: "Arch Linux",
  debian: "Debian",
  alpine: "Alpine Linux",
};

export default function ReviewStep({
  mode, dev, arch, flavor, initSys, buildMode, desktop, dm, pkgs, onStart,
}) {
  const sizeMB = desktop === "none" ? 320 : 1900;
  const sizeLabel = desktop === "none" ? "about 320 MB" : "about 1.9 GB";
  const timeLabel = desktop === "none" ? "around 5–10 minutes" : "around 10–20 minutes";

  if (mode !== "advanced") {
    return <ReviewBasic dev={dev} flavor={flavor} desktop={desktop} pkgs={pkgs}
      sizeLabel={sizeLabel} timeLabel={timeLabel} onStart={onStart} />;
  }

  return (
    <React.Fragment>
      <Head c="STEP 05 / 06 · REVIEW" t="Ready to build"
        s="Peacock will cross-compile and assemble a flashable image. This can take several minutes." />
      <div className="mbody fade">
        <div className="summary">
          <SRow k="Device" v={dev.code} /><SRow k="Architecture" v={arch} />
          <SRow k="Distribution" v={flavor} /><SRow k="Init system" v={initSys} />
          <SRow k="Build mode" v={buildMode} /><SRow k="Desktop" v={desktop} />
          <SRow k="Display manager" v={dm} />
          <SRow k="Extra packages" v={pkgs.length ? pkgs.length + " selected" : "none"} />
          <SRow k="Output path" v={`~/.local/var/peacock/${dev.code}.img`} />
          <SRow k="Estimated size" v={`${sizeMB} MB`} />
        </div>
        <div className="callout"><PK src={HEAD} style={{ width: 30, height: 36, flex: "0 0 30px" }} className="pkgrad" />
          <div className="ct"><b>Output</b> → <span style={{ fontFamily: "var(--mono)", fontSize: 12.5 }}>~/.local/var/peacock/{dev.code}.img</span><br />
            Estimated size ≈ {sizeMB === 320 ? "320 MB" : "1.9 GB"} · ~6 ports built locally.</div></div>
      </div>
    </React.Fragment>
  );
}

function ReviewBasic({ dev, flavor, desktop, pkgs, sizeLabel, timeLabel, onStart }) {
  const [open, setOpen] = React.useState(false);
  const flavorName = FRIENDLY_FLAVOR[flavor] || flavor;
  const desktopName = FRIENDLY_DESKTOP[desktop] || desktop;
  const desktopShort = desktop === "none" ? "no graphical desktop" : (desktop === "plasma-mobile" ? "Plasma" : desktop[0].toUpperCase() + desktop.slice(1)) + " mobile desktop";

  return (
    <React.Fragment>
      <Head c="STEP 05 / 06 · REVIEW" t="Ready to build"
        s="Everything's set. Tap the button below to start. You can keep using your computer while it builds." />
      <div className="mbody fade">
        <div className="rvbasic">
          <div className="rvhero">
            <PK src={FULL} className="rvpk pkgrad" />
            <div className="rvshine" />
          </div>
          <p className="rvlede">
            You're about to build PeacockOS for the <b>{dev.name}</b>. It will use
            the <b>{flavorName}</b> base with {desktop === "none" ? <b>{desktopName}</b> : <React.Fragment>the <b>{desktopShort}</b></React.Fragment>}.
            The image will be <b>{sizeLabel}</b> when done. Building takes {timeLabel} depending on your computer.
          </p>

          <button className={"rvtog" + (open ? " on" : "")} onClick={() => setOpen(o => !o)}>
            <span className="rvtogcaret">{open ? "▾" : "▸"}</span> What's inside
          </button>
          {open && (
            <ul className="rvinside">
              <li><span className="rvdot" /> Linux kernel tuned for your <b>{dev.name}</b></li>
              <li><span className="rvdot" /> {flavorName} system tools (the core programs)</li>
              {desktop !== "none" && <li><span className="rvdot" /> {desktopName}</li>}
              {desktop !== "none" && <li><span className="rvdot" /> A login screen</li>}
              {pkgs.length > 0 && (
                <li><span className="rvdot" /> The {pkgs.length} extra app{pkgs.length === 1 ? "" : "s"} you picked
                  <span className="rvinpkgs"> ({pkgs.join(", ")})</span></li>
              )}
              <li><span className="rvdot" /> Wi-Fi, Bluetooth, and battery management</li>
            </ul>
          )}

          <div className="rvact">
            <Btn variant="grad" ar="→" onClick={onStart}>Build &amp; flash</Btn>
            <span className="rvactnote">Or flip <b>Advanced</b> in the top-right to double-check every setting.</span>
          </div>
        </div>
      </div>
    </React.Fragment>
  );
}
