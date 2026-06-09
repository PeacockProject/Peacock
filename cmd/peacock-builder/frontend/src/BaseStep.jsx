/* BaseStep.jsx — Step 2 of the build wizard ("Base flavor & init")
 *
 * Basic mode: one big dropdown for the distribution + plain-English copy
 * underneath + a small infograph card on the right. Init system, arch and
 * build mode are hidden and set to sensible defaults derived from the
 * device + host.
 *
 * Advanced mode: the original 4-segment grid so power users still have
 * every knob. A small "reset to defaults" link sits at the top. */
import React from "react";
import { Head, Field, Seg } from "./shared.jsx";

/* Plain-English copy for each base. Tone is friendly, explicitly
 * non-technical, optimised for people who have never installed Linux. */
const FLAVORS = [
  {
    id: "arch",
    name: "Arch Linux",
    sub: "Recommended · newest software",
    blurb:
      "PeacockOS will be built on top of Arch Linux. Arch is known for being lean, up-to-date, and gives you the newest software the day it ships. It's the recommended choice for most people — Peacock is best-tested here.",
    chips: ["Linux kernel", "GNU/glibc", "~14,000 packages", "pacman"],
    accent: "#2E86C8",
  },
  {
    id: "debian",
    name: "Debian",
    sub: "Rock-solid · widely supported",
    blurb:
      "PeacockOS will be built on top of Debian. Debian is the boring, rock-solid choice. Software is a bit older but extensively tested. Pick this if you want stability over having the newest features.",
    chips: ["Linux kernel", "GNU/glibc", "~60,000 packages", "apt"],
    accent: "#6C63D8",
  },
  {
    id: "alpine",
    name: "Alpine Linux",
    sub: "Tiny · for tinkerers",
    blurb:
      "PeacockOS will be built on top of Alpine Linux. Alpine is tiny — your phone will boot faster and use less battery. Cost: some software you find online won't run without extra steps (musl libc, not glibc). For tinkerers.",
    chips: ["Linux kernel", "musl libc", "~11,000 packages", "apk"],
    accent: "#23B7AE",
  },
];

const DEFAULTS = { initSys: "openrc", mode: "qemu-user" };

export default function BaseStep({
  mode, flavor, setFlavor, initSys, setInitSys, arch, setArch, buildMode, setBuildMode,
}) {
  const f = FLAVORS.find(x => x.id === flavor) || FLAVORS[0];

  if (mode !== "advanced") {
    return (
      <React.Fragment>
        <Head c="STEP 02 / 06 · BASE"
          t="Pick a foundation"
          s="PeacockOS is built on top of another Linux. This choice affects how new your apps are and how things get installed. Don't worry — you can change it later." />
        <div className="mbody fade">
          <div className="basicbase">
            <div className="bbcol">
              <label className="bblbl">Base system</label>
              <div className="bbsel">
                <select className="bbselect" value={flavor} onChange={e => setFlavor(e.target.value)}>
                  {FLAVORS.map(o => (
                    <option key={o.id} value={o.id}>{o.name} — {o.sub}</option>
                  ))}
                </select>
                <span className="bbcaret">▾</span>
              </div>
              <p className="bbblurb">{f.blurb}</p>
              <div className="bbnote">
                <span>Defaults set for you:</span>
                <span className="bbk">init <b>openrc</b></span>
                <span className="bbk">arch <b>{arch}</b></span>
                <span className="bbk">build <b>{buildMode}</b></span>
                <span className="bbflip">Need to change these? Flip <b>Advanced</b> in the top-right.</span>
              </div>
            </div>
            <aside className="bbgraph" style={{ "--bbacc": f.accent }}>
              <div className="bbghd">What's inside {f.name}</div>
              <div className="bbgstack">
                {f.chips.map((c, i) => (
                  <div key={c} className="bbgrow" style={{ animationDelay: (i * 60) + "ms" }}>
                    <span className="bbgdot" />
                    <span className="bbglbl">{c}</span>
                  </div>
                ))}
              </div>
              <div className="bbgfoot">+ PeacockOS on top</div>
            </aside>
          </div>
        </div>
      </React.Fragment>
    );
  }

  const resetDefaults = () => {
    setInitSys(DEFAULTS.initSys);
    setBuildMode(DEFAULTS.mode);
  };

  return (
    <React.Fragment>
      <Head c="STEP 02 / 06 · BASE" t="Base flavor & init"
        s="The base distribution Peacock bootstraps into the rootfs, and how the system boots." />
      <div className="mbody fade">
        <div className="advtop">
          <span className="advnote">All knobs exposed.</span>
          <button className="advlink" onClick={resetDefaults}>Reset to defaults</button>
        </div>
        <Field l="Distribution" sub="base-distro">
          <Seg v={flavor} set={setFlavor} opts={["arch", "debian", "alpine"]} /></Field>
        <Field l="Init system">
          <Seg v={initSys} set={setInitSys} opts={["systemd", "openrc"]} /></Field>
        <Field l="Architecture" sub="from device">
          <Seg v={arch} set={setArch} opts={["aarch64", "armv7h", "x86_64"]} /></Field>
        <Field l="Build mode" sub="cross-compile">
          <Seg v={buildMode} set={setBuildMode} opts={["qemu-user", "native", "cross"]} /></Field>
      </div>
    </React.Fragment>
  );
}
