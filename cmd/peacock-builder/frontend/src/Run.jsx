/* Run.jsx — shared live-progress screen + log scripts + done screens
 *
 * Phase 1: simulated buildScript / installScript so the UI runs standalone
 * via `npm run dev` with no backend. Phase 4 will replace these with
 * subscriptions to window.runtime.EventsOn("build:log", ...) once the
 * Wails app starts emitting real runner output. */
import React from "react";
import { PK, Btn, FULL } from "./shared.jsx";

const L = (t, prog, node) => ({ t, prog, node });

export const BUILD_PHASES = [
  { at: 0, label: "Resolving deps" }, { at: 18, label: "Building kernel" },
  { at: 34, label: "Building busybox" }, { at: 48, label: "Initramfs" },
  { at: 60, label: "Rootfs" }, { at: 80, label: "Configuring" }, { at: 92, label: "Disk image" },
];

export function buildScript(dev, desktop) {
  return [
    L("12:04:18", 6, <span>Resolving dependencies…</span>),
    L("12:04:20", 12, <span>→ <span className="b">6</span> local ports · <span className="b">142</span> remote packages</span>),
    L("12:04:26", 20, <span>building <span className="b">linux-{dev.code}</span> 3.4.0…</span>),
    L("12:05:02", 30, <span><span className="g">✓</span> zImage <span className="y">(4.2 MB)</span></span>),
    L("12:05:03", 34, <span><span className="g">✓</span> modules.tar.gz</span>),
    L("12:05:10", 40, <span>building <span className="b">busybox</span> 1.36.1…</span>),
    L("12:05:24", 46, <span><span className="g">✓</span> busybox</span>),
    L("12:05:25", 50, <span>peacock-mkinitfs build --device {dev.code}</span>),
    L("12:05:40", 56, <span><span className="g">✓</span> initramfs.cpio.gz</span>),
    L("12:05:42", 60, <span>setting up image build chroot…</span>),
    L("12:06:01", 66, <span>installing packages to rootfs…</span>),
    L("12:06:03", 72, <span>&nbsp;&nbsp;→ {desktop === "none" ? "base" : desktop} mesa <span className="b">+142</span></span>),
    L("12:06:30", 80, <span>enabling services · staging extlinux…</span>),
    L("12:06:45", 88, <span>creating disk image (1920 MB)…</span>),
    L("12:06:58", 96, <span>mkfs.ext4 · <span className="b">ROOT</span></span>),
    L("12:07:05", 100, <span><span className="g">✓</span> build complete</span>),
  ];
}

export const INSTALL_PHASES = [
  { at: 0, label: "Partitioning" }, { at: 20, label: "Formatting" },
  { at: 34, label: "Copying system" }, { at: 78, label: "Bootloader" }, { at: 92, label: "Finishing" },
];

export function installScript(disk, user) {
  return [
    L("·", 5, <span>Creating partition table on <span className="b">{disk.node}</span>…</span>),
    L("·", 12, <span><span className="g">✓</span> {disk.node}1 boot · {disk.node}2 root</span>),
    L("·", 20, <span>mkfs.ext4 -L ROOT {disk.node}2…</span>),
    L("·", 30, <span><span className="g">✓</span> filesystems ready</span>),
    L("·", 36, <span>Copying PeacockOS to target…</span>),
    L("·", 52, <span>&nbsp;&nbsp;unpacking rootfs · 41,206 files</span>),
    L("·", 68, <span>&nbsp;&nbsp;<span className="y">rsync</span> /usr /etc /var · 1.7 GB</span>),
    L("·", 76, <span><span className="g">✓</span> system copied</span>),
    L("·", 80, <span>Creating user <span className="b">{user || "peacock"}</span> · setting hostname</span>),
    L("·", 86, <span>Installing bootloader (extlinux)…</span>),
    L("·", 92, <span>Generating initramfs · fstab…</span>),
    L("·", 100, <span><span className="g">✓</span> installation complete</span>),
  ];
}

export function RunScreen({ script, title, meta, phases, onDone }) {
  const [n, setN] = React.useState(0);
  const prog = n > 0 ? script[n - 1].prog : 0;
  React.useEffect(() => {
    if (n >= script.length) { const t = setTimeout(onDone, 900); return () => clearTimeout(t); }
    const t = setTimeout(() => setN(n + 1), n === 0 ? 300 : 300 + Math.random() * 240);
    return () => clearTimeout(t);
  }, [n]);
  const phase = phases.reduce((a, p) => (prog >= p.at ? p.label : a), phases[0].label);
  const lines = script.slice(0, n);
  const recent = lines.slice(-5);
  const [showLog, setShowLog] = React.useState(false);
  return (
    <div className="rprog">
      <div className="rpl">
        <div className="glow" />
        <div className="meta">{meta}</div>
        <div className="bigpct">{Math.round(prog)}<span className="pp">%</span></div>
        <h2>{title}</h2>
        <div className="phase">{prog >= 100 ? "Complete" : phase + "…"}</div>
        <div className="rtrack"><i style={{ width: prog + "%" }} /></div>
        <div className="rsteps">{phases.map((p, i) => {
          const cur = phase === p.label && prog < 100;
          const done = prog > p.at && phase !== p.label || prog >= 100;
          return <span key={i} className={"stp" + (done ? " done" : cur ? " cur" : "")}><span className="d" />{p.label}</span>;
        })}</div>
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
              {n < script.length && <div className="ln"><span className="cur">▍</span></div>}
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
