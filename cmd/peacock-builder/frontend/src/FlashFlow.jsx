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

/* ===== F2: per-brand unlock instructions ================================ */

/* Each brand entry yields a friendly title + ordered step list. Copy is
 * written for someone who has never heard of fastboot — every piece of
 * jargon is translated inline. dialCode + waiting periods are kept verbatim
 * so the OPPO/Xiaomi facts can be fact-checked by the maintainer. */
const UNLOCK_BRANDS = {
  oppo: {
    title: "OPPO unlock — the slow but official path",
    blurb: "OPPO requires a mandatory 7-day waiting period after you apply. Start this early!",
    steps: [
      "Open Settings → About Phone, then tap the build number 7 times. You'll see a small toast: \"You are now a developer.\"",
      "Go back to Settings → System → Developer Options and turn on \"OEM unlocking\" (this lets us replace the system later).",
      "Open OPPO's \"Deep Testing\" app. If you don't see it, dial *#*#82284#*#* on the phone keypad to launch it.",
      "Inside Deep Testing, tap \"Apply for permission\" and follow the prompts. OPPO will make you wait 7 days — this is set in stone by OPPO, not us.",
      "After the 7-day wait, come back. Hold Power + Volume Down to reboot into \"fastboot mode\" (a small text screen that lets your computer write to the phone).",
      "Plug the phone into your computer with a USB cable. On your computer, run: fastboot oem unlock — and confirm on the phone when it asks.",
    ],
  },
  xiaomi: {
    title: "Xiaomi unlock — needs Mi Unlock on Windows",
    blurb: "Xiaomi requires a 7-14 day waiting period and their Mi Unlock tool, which is Windows-only.",
    steps: [
      "Open Settings → About Phone, then tap \"MIUI version\" 7 times. You'll see \"You are now a developer.\"",
      "Go to Settings → Additional Settings → Developer Options. Turn on \"OEM unlocking\" and \"USB debugging\".",
      "In the same screen, find \"Mi Unlock status\" and add your Mi account (you'll need to sign in).",
      "Install \"Mi Unlock\" on a Windows PC (Xiaomi's official site). Mac / Linux are not supported by Xiaomi for this step.",
      "Open Mi Unlock, sign in with the same Mi account, plug your phone in via USB, and click \"Unlock\". Xiaomi will tell you to come back in 7-14 days.",
      "After the wait, repeat the Mi Unlock step. This time it goes through. The phone reboots automatically.",
      "Hold Power + Volume Down to enter fastboot mode (a small text screen for flashing).",
    ],
  },
  samsung: {
    title: "Samsung unlock — Download Mode + Heimdall",
    blurb: "Samsung uses \"Download Mode\" instead of fastboot, and Heimdall is the open-source tool that talks to it.",
    steps: [
      "Open Settings → About Phone, tap \"Build number\" 7 times until you see \"You are now a developer.\"",
      "Go to Settings → Developer Options and turn on both \"OEM unlocking\" and \"USB debugging\".",
      "Power the phone OFF completely.",
      "Hold Power + Volume Down + Home at the same time. Keep holding until you see a yellow warning screen — this is \"Download Mode\".",
      "Press Volume Up to confirm you want to continue into Download Mode.",
      "Install Heimdall on your computer (heimdall-flash on Linux, or the .pkg on macOS). Heimdall is the free replacement for Samsung's Windows-only Odin tool.",
      "Plug the phone into your computer with a USB cable. On first boot to Download Mode after enabling OEM unlocking, the phone itself shows a prompt — confirm OEM unlock there with Volume Up.",
    ],
  },
  pine: {
    title: "PinePhone — nothing to do",
    blurb: "Good news — PinePhone ships unlocked from the factory. There's no bootloader to fight.",
    steps: [
      "Skip this step — your PinePhone is already ready.",
      "Just make sure the phone is powered on and unlocked when you plug it in.",
    ],
    autoConfirm: true,
  },
  fairphone: {
    title: "Fairphone 4 unlock — short and friendly",
    blurb: "Fairphone is one of the easier ones. No waiting period, but you do need a one-time code from their site.",
    steps: [
      "Open Settings → About Phone, tap \"Build number\" 7 times until you see \"You are now a developer.\"",
      "Go to Settings → System → Developer Options and turn on \"OEM unlocking\".",
      "Power the phone off. Hold Power + Volume Up to reboot into fastboot mode (a small text screen for flashing).",
      "On the Fairphone support site, request your phone's unlock code (you'll need the phone's serial number — it's shown on the fastboot screen).",
      "Plug the phone in via USB. On your computer, run: fastboot oem unlock <the-code-they-sent-you>",
    ],
  },
  x86: {
    title: "x86 PC — nothing to unlock",
    blurb: "A regular PC has no locked bootloader. Just make sure your BIOS lets you boot from USB.",
    steps: [
      "Skip this step — there's no bootloader to unlock on a regular PC.",
    ],
    autoConfirm: true,
  },
};

function StepUnlock({ dev, build, onCancel, onBack, onNext }) {
  const ports = portsFor(dev);
  const brand = ports.brand;
  const info = UNLOCK_BRANDS[brand] || UNLOCK_BRANDS.oppo;
  /* PinePhone + x86 default-confirm since there's nothing for the user to actually do. */
  const [confirmed, setConfirmed] = React.useState(!!info.autoConfirm);
  const [open, setOpen] = React.useState(brand);
  const ready = confirmed && build.done;
  /* the user can ALWAYS click Continue once both gates are met. If the
   * build is still going, show "Still building…" instead of disabling
   * silently — gives them a reason. */
  const hint = !confirmed
    ? "tick the box below once your phone is unlocked"
    : (build.done ? "ready to continue" : "still building image · " + Math.round(build.progress) + "%");
  return (
    <div className="ff" data-step="unlock">
      <FFTop title="Step 2 of 5 · Unlock your phone" onCancel={onCancel} />
      <BuildBanner build={build} />
      <div className="ff-body">
        <div className="ff-unlock">
          <p className="ff-lead">
            Phones ship locked — they only run software signed by their maker.
            We need to flip that switch first. This is the slow part of the
            install: most makers add a waiting period. Start it now and let
            it run in the background while we build your image.
          </p>
          {Object.entries(UNLOCK_BRANDS).map(([k, v]) => {
            const expanded = k === open;
            const isYou = k === brand;
            return (
              <div key={k} className={"ff-acc" + (expanded ? " open" : "") + (isYou ? " mine" : "")}>
                <div className="ff-acc-head" onClick={() => setOpen(expanded ? null : k)}>
                  <span className="ff-acc-brand">{k}</span>
                  <span className="ff-acc-title">{v.title}</span>
                  {isYou && <span className="ff-acc-pill">your phone</span>}
                  <span className="ff-acc-chev">{expanded ? "−" : "+"}</span>
                </div>
                {expanded && (
                  <div className="ff-acc-body">
                    <p className="ff-acc-blurb">{v.blurb}</p>
                    <ol className="ff-acc-steps">
                      {v.steps.map((s, i) => <li key={i}>{s}</li>)}
                    </ol>
                  </div>
                )}
              </div>
            );
          })}
          {!info.autoConfirm && (
            <label className={"ff-ack alone" + (confirmed ? " on" : "")}>
              <span className="ff-check" onClick={() => setConfirmed(c => !c)}>{confirmed ? "✓" : ""}</span>
              <span>I've unlocked the bootloader and my phone is in fastboot mode.</span>
            </label>
          )}
        </div>
      </div>
      <FFFoot onBack={onBack} hint={hint} onNext={onNext} nextDisabled={!ready} nextLabel="Continue" nextVariant="grad" />
    </div>
  );
}

/* Persistent "Image is building…" status banner. Lives above the unlock
 * body and morphs to "Image ready." with a green dot when the simulated
 * build job completes. The two-color glow shifts based on done state. */
function BuildBanner({ build }) {
  const pct = Math.round(build.progress);
  const done = build.done;
  /* tiny inline ETA: assume ~12s remaining at 60%, scale linearly. The real
   * build job will replace this with a server-emitted eta_sec field. */
  const etaSec = done ? 0 : Math.max(2, Math.round((100 - pct) * 0.18));
  return (
    <div className={"ff-banner" + (done ? " done" : "")} role="status">
      <span className={"ff-bd" + (done ? " g" : "")} />
      <span className="ff-bt">
        {done ? "Image ready." : "Image is building…"}
      </span>
      <span className="ff-bp">{done ? "100%" : pct + "%"}</span>
      <div className="ff-btrack"><i style={{ width: pct + "%" }} /></div>
      <span className="ff-bph">{done ? "system image written" : build.phase + (etaSec ? " · ~" + etaSec + "s left" : "")}</span>
    </div>
  );
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
        {sub === "unlock" && <StepUnlock dev={dev} build={build} onCancel={cancel} onBack={() => setSub("warn")} onNext={() => setSub("connect")} />}
        {/* connect / flash / done land in subsequent commits */}
        {(sub === "connect" || sub === "flash" || sub === "done") && <div className="ff-tbd">step "{sub}" — coming next commit</div>}
        <DiscardModal open={discardOpen} onKeep={keep} onDiscard={discard} />
      </div>
    </AppShell>
  );
}
