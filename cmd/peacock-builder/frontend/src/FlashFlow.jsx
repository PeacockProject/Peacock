/* FlashFlow.jsx — Build + flash to a real device.
 *
 * This is the post-Review wizard the maintainer wired up to the "Build & flash"
 * CTA. It overlaps the simulated image build with the user's slow bootloader
 * unlock work so the flow feels instant by the time they're back at the PC.
 *
 * Sub-steps:
 *   F1  Warn  — data-loss confirmation (kicks off the background build job).
 *   F2  Unlock — per-brand bootloader-unlock instructions + live build banner.
 *   F3  Connect — plug-in detection (mock 4s).
 *   F4  Flash  — bootloader → recovery → system, live progress + log.
 *   F5  Done   — Welcome screen, next steps.
 *
 * Phase 1 (today): all jobs are simulated via the same log-script pattern as
 * Run.jsx — see useBuildJob / useFlashJob below. Phase 4 will swap the
 * timers for Wails event subscriptions (window.runtime.EventsOn). */

import React from "react";
import { AppShell, PK, Btn, FULL, HEAD } from "./shared.jsx";
import { buildScript, BUILD_PHASES } from "./Run.jsx";

/* ===== per-device port wiring =====
 *
 * Mirrors peacock-ports/device/. Three independent flags so we can render
 * "skip this phase" for devices that don't have the asset yet.
 *   bootloader: which custom bootloader image is flashed first (or null).
 *   recovery:   PRP recovery ramdisk port (all supported devices have one).
 *   system:     the actual rootfs/system image baked by the build job.
 *
 * For the bootloader: OPPO/MTK uses minkernel (a tiny preloader stub),
 * Snapdragon devices use lk2nd (the community little-kernel fork). x86 and
 * PinePhone have no second-stage bootloader to flash here. Fairphone 4 is
 * listed as "TBD" because the port hasn't landed yet — we mock-skip it. */
const PORTS = {
  "oppo-a16":      { brand: "oppo",      bootloader: "minkernel-oppo-a16",   recovery: "prp-oppo-a16",      system: "linux-oppo-a16" },
  "xiaomi-daisy":  { brand: "xiaomi",    bootloader: "lk2nd-xiaomi-daisy",   recovery: "prp-xiaomi-daisy",  system: "linux-xiaomi-daisy-prp" },
  "samsung-jflte": { brand: "samsung",   bootloader: "lk2nd-samsung-jflte",  recovery: "prp-samsung-jflte", system: "linux-samsung-jflte" },
  "pine-pp":       { brand: "pine",      bootloader: null,                   recovery: null,                system: "linux-pinephone" },
  "fairphone-fp4": { brand: "fairphone", bootloader: null,                   recovery: null,                system: "linux-fairphone-fp4" },
  "generic-x86":   { brand: "x86",       bootloader: null,                   recovery: null,                system: "linux-generic-x86" },
};
function portsFor(dev) {
  if (!dev) return PORTS["oppo-a16"];
  return PORTS[dev.id] || PORTS[dev.code] || { brand: "generic", bootloader: null, recovery: null, system: "linux-" + (dev.code || "device") };
}

/* ===== background build job (F1 kick-off, F2 banner, F3 gate) =====
 *
 * A custom hook that drives the same simulated buildScript() lines used in
 * Run.jsx, but emits {progress, phase, done} so callers can render a small
 * banner instead of the full RunScreen layout. Once we have real Wails
 * events this is the single place to swap in EventsOn("build:log",…). */
function useBuildJob(dev, desktop, armed) {
  const [n, setN] = React.useState(0);
  const script = React.useMemo(() => buildScript(dev || { code: "x" }, desktop), [dev && dev.code, desktop]);
  React.useEffect(() => {
    if (!armed) return;
    if (n >= script.length) return;
    const t = setTimeout(() => setN(n + 1), n === 0 ? 400 : 380 + Math.random() * 300);
    return () => clearTimeout(t);
  }, [armed, n, script.length]);
  const prog = n > 0 ? script[n - 1].prog : 0;
  const phase = BUILD_PHASES.reduce((a, p) => (prog >= p.at ? p.label : a), BUILD_PHASES[0].label);
  return { progress: prog, phase, done: prog >= 100, lines: script.slice(0, n) };
}

/* ===== F1: data-loss warning ============================================ */
function StepWarn({ dev, onCancel, onBack, onNext }) {
  const [ack1, setAck1] = React.useState(false);
  const [ack2, setAck2] = React.useState(false);
  const ready = ack1 && ack2;
  const name = (dev && dev.name) || "phone";
  return (
    <div className="ff" data-step="warn">
      <FFTop title="Step 1 of 5 · Before we start" onCancel={onCancel} />
      <div className="ff-body">
        <div className="ff-warn">
          <div className="ff-warn-icon" aria-hidden="true">
            <PK src={HEAD} className="pkw pkgrad" />
            <span className="ff-warn-bang">!</span>
          </div>
          <div className="ff-warn-tag">DATA LOSS WARNING</div>
          <h1 className="ff-warn-h1">This will erase your phone.</h1>
          <p className="ff-warn-body">
            Continuing will completely wipe your <b>{name}</b> — apps, photos,
            accounts, everything. PeacockOS replaces the system that came on
            it. There is no undo.
          </p>
          <div className="ff-acks">
            <label className={"ff-ack" + (ack1 ? " on" : "")}>
              <span className="ff-check" onClick={() => setAck1(a => !a)}>{ack1 ? "✓" : ""}</span>
              <span>I've backed up anything I want to keep from this phone.</span>
            </label>
            <label className={"ff-ack" + (ack2 ? " on" : "")}>
              <span className="ff-check" onClick={() => setAck2(a => !a)}>{ack2 ? "✓" : ""}</span>
              <span>I understand this is permanent.</span>
            </label>
          </div>
        </div>
      </div>
      <FFFoot
        onBack={onBack}
        hint={ready ? "ready to continue" : "tick both boxes to continue"}
        onNext={onNext}
        nextDisabled={!ready}
        nextLabel="Continue"
        nextVariant="warn"
      />
    </div>
  );
}

/* ===== shared chrome ==================================================== */
function FFTop({ title, onCancel }) {
  return (
    <div className="ff-top">
      <div className="ff-crumb">{title}</div>
      <button className="ff-cancel" onClick={onCancel} title="Cancel and return home">Cancel ×</button>
    </div>
  );
}
function FFFoot({ onBack, hint, onNext, nextDisabled, nextLabel = "Continue", nextVariant = "primary" }) {
  return (
    <div className="ff-foot">
      <Btn variant="subtle" onClick={onBack}>‹ Back</Btn>
      <div className="sp" />
      {hint ? <span className="ff-hint">{hint}</span> : null}
      <Btn variant={nextVariant === "warn" ? "warn" : nextVariant === "grad" ? "grad" : "primary"}
        ar="›" disabled={nextDisabled} onClick={onNext}>{nextLabel}</Btn>
    </div>
  );
}

/* ===== cancel-discard modal ============================================ */
function DiscardModal({ open, onKeep, onDiscard }) {
  if (!open) return null;
  return (
    <div className="ff-modal-wrap" role="dialog">
      <div className="ff-modal-back" onClick={onKeep} />
      <div className="ff-modal">
        <div className="ff-modal-tag">CANCEL FLASHING</div>
        <h2>Discard progress?</h2>
        <p>You'll lose the image build in progress and any confirmation you've ticked. Your phone is unaffected so far.</p>
        <div className="ff-modal-acts">
          <Btn variant="ghost" onClick={onKeep}>Keep going</Btn>
          <Btn variant="warn" onClick={onDiscard}>Discard</Btn>
        </div>
      </div>
    </div>
  );
}

/* ===== top-level driver =================================================
 *
 * State machine: warn → unlock → connect → flash → done. The background
 * build job is armed when we enter "warn" so it runs in parallel with the
 * user reading the unlock instructions. */
export default function FlashFlow({ dev, flavor, initSys, desktop, onHome, appClass }) {
  const [sub, setSub] = React.useState("warn");
  const [discardOpen, setDiscardOpen] = React.useState(false);
  const build = useBuildJob(dev, desktop, true); // armed immediately at F1 entry
  const ports = portsFor(dev);

  const cancel = () => setDiscardOpen(true);
  const keep = () => setDiscardOpen(false);
  const discard = () => { setDiscardOpen(false); onHome(); };

  const status = (
    <React.Fragment>
      <span className="pd" /><span>{dev ? dev.code : "no device"}</span>
      <span className="sep">·</span><span>flash · {ports.brand}</span>
      <span className="r">
        <span className="crumb">{
          sub === "warn" ? "Warning" : sub === "unlock" ? "Unlock" : sub === "connect" ? "Connect" : sub === "flash" ? "Flashing" : "Done"
        }</span>
        <span className="sep">·</span>
        <span>step {{ warn: 1, unlock: 2, connect: 3, flash: 4, done: 5 }[sub]} / 5</span>
      </span>
    </React.Fragment>
  );

  return (
    <AppShell appClass={appClass} title={<span>Flash to device <span className="dim">· {dev && dev.code}</span></span>} status={status}>
      <div className="ffwrap">
        {sub === "warn" && <StepWarn dev={dev} onCancel={cancel} onBack={onHome} onNext={() => setSub("unlock")} />}
        {/* unlock / connect / flash / done land in subsequent commits */}
        {sub !== "warn" && <div className="ff-tbd">step "{sub}" — coming next commit</div>}
        <DiscardModal open={discardOpen} onKeep={keep} onDiscard={discard} />
      </div>
    </AppShell>
  );
}
