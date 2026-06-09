/* FlashFlow.jsx — Build + flash to a real device.
 *
 * This is the post-Review wizard the maintainer wired up to the "Build & flash"
 * CTA. It overlaps the simulated image build with the user's slow bootloader
 * unlock work so the flow feels instant by the time they're back at the PC.
 *
 * Sub-steps:
 *   F0  Splash — short peacock splash that morphs (scales down + slides to
 *                the top-left of the wizard chrome) into the spot where the
 *                persistent build banner will live. Kicks off the background
 *                build job at mount.
 *   F1  Warn  — data-loss confirmation.
 *   F2  Unlock — matched-brand bootloader-unlock instructions, with an
 *                "already unlocked" skip option that swaps in a green
 *                confirmation card. Build progress shows in the persistent
 *                top banner above, not inline.
 *   F3  Connect — plug-in detection (mock 4s).
 *   F4  Flash  — bootloader → recovery → system, live progress + log.
 *   F5  Done   — Welcome screen, next steps.
 *
 * Persistent build banner: lives at the top-left of the wizard chrome across
 * F1..F4. Click it to open the full RunScreen overlay so power users can
 * watch the live log without losing flash-flow state.
 *
 * Phase 1 (today): all jobs are simulated via the same log-script pattern as
 * Run.jsx — see useBuildJob / useFlashJob below. Phase 4 will swap the
 * timers for Wails event subscriptions (window.runtime.EventsOn). */

import React from "react";
import { AppShell, PK, Btn, FULL, HEAD } from "./shared.jsx";
import { buildScript, BUILD_PHASES, RunScreen } from "./Run.jsx";

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

/* ===== background build job (F0 kick-off, persistent banner, F3 gate) =====
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
  return { progress: prog, phase, done: prog >= 100, lines: script.slice(0, n), script };
}

/* ===== F0: splash → top-bar morph =======================================
 *
 * The splash is a full-stage hero (peacock + one line). After ~1.5s it enters
 * a "docking" state: a single coordinated CSS transition scales the peacock
 * down + translates it to the top-left, the label fades out, and the body
 * fades in behind it. After the docking transition (~720ms) the driver
 * advances to F1 and the docked spot becomes the home for the persistent
 * build banner.
 *
 * State-machine driver: a deterministic setTimeout chain, NOT the CSS
 * `transitionend` event. The previous implementation relied on
 * `transitionend` on `propertyName === "transform"` to advance from F0 → F1,
 * which was fragile — if the splash element unmounted, had pointer-events
 * blocked, or the keyframe entry animation collided with the transition,
 * the event never fired and the screen sat blank on "step 0 / 5". The CSS
 * animation still runs visually; the timer is just the state driver.
 *
 * Timing budget (matches styles/app.css):
 *   HOLD_MS   1500   visible centered splash
 *   DOCK_MS    720   matches `.ff-splash-pk` transition:transform .72s
 *   BUFFER_MS   80   safety pad before advancing to F1
 *
 * StrictMode safety: timer IDs live in a ref so the cleanup pass in the
 * double-mount cycle doesn't leave a dangling timer that fires onDone
 * twice. We also flip a hasFiredRef before calling onDone so the
 * belt-and-suspenders `transitionend` early-out can't re-fire it. */
const SPLASH_HOLD_MS = 1500;
const SPLASH_DOCK_MS = 720;
const SPLASH_BUFFER_MS = 80;

function StepSplash({ onDone }) {
  const [docking, setDocking] = React.useState(false);
  const dockTimerRef = React.useRef(null);
  const doneTimerRef = React.useRef(null);
  const rafRef = React.useRef(null);
  const firedRef = React.useRef(false);
  /* Stash onDone in a ref so the schedule effect can have a stable [] deps
   * list. Without this, the parent passes a fresh `() => setSub("warn")`
   * closure each render — when we call setDocking(true) the effect would
   * re-run, clear its own timers, and reschedule, looping forever. */
  const onDoneRef = React.useRef(onDone);
  React.useEffect(() => { onDoneRef.current = onDone; }, [onDone]);

  const fireOnce = React.useCallback(() => {
    if (firedRef.current) return;
    firedRef.current = true;
    onDoneRef.current && onDoneRef.current();
  }, []);

  React.useEffect(() => {
    /* Schedule docking after the hold period. Wrap setDocking(true) in a
     * requestAnimationFrame so React's render of the centered state has
     * committed to at least one paint frame before the `.docking` class is
     * added — without this, the browser can batch the initial render and
     * the class change into the same frame and skip the transition
     * entirely (manifests as "splash disappears in 0ms"). */
    dockTimerRef.current = setTimeout(() => {
      rafRef.current = requestAnimationFrame(() => {
        setDocking(true);
      });
    }, SPLASH_HOLD_MS);

    /* Deterministic advance to F1: hold + dock + small buffer. The CSS
     * transition is the visual; this timer drives the state machine. */
    doneTimerRef.current = setTimeout(
      fireOnce,
      SPLASH_HOLD_MS + SPLASH_DOCK_MS + SPLASH_BUFFER_MS,
    );

    return () => {
      if (dockTimerRef.current) clearTimeout(dockTimerRef.current);
      if (doneTimerRef.current) clearTimeout(doneTimerRef.current);
      if (rafRef.current) cancelAnimationFrame(rafRef.current);
    };
    /* eslint-disable-next-line react-hooks/exhaustive-deps -- fireOnce is
     * stable (empty deps via the ref shim above), and we explicitly want
     * this effect to run exactly once at mount. */
  }, []);

  /* Belt-and-suspenders: if the peacock's transform transition does end
   * cleanly we advance a hair early, but the timer is still the source of
   * truth — fireOnce() is idempotent. */
  const onTransitionEnd = (e) => {
    if (!docking) return;
    if (e.propertyName !== "transform") return;
    if (!(e.target && e.target.classList && e.target.classList.contains("ff-splash-pk"))) return;
    fireOnce();
  };

  return (
    /* pointer-events:none so the splash overlay doesn't trap clicks on
     * the wizard's Cancel chrome that sits underneath it. The splash has
     * no interactive elements of its own. */
    <div
      className={"ff-splash" + (docking ? " docking" : "")}
      style={{ pointerEvents: "none" }}
      onTransitionEnd={onTransitionEnd}
    >
      <div className="ff-splash-stage">
        <div className="ff-splash-aura" />
        <PK src={FULL} className="ff-splash-pk pkgrad" />
        <div className="ff-splash-line">Building your image…</div>
      </div>
    </div>
  );
}

/* ===== persistent top-docked build banner ===============================
 *
 * Rendered by the driver across F1..F4. Visually it sits at the wizard's
 * top-left (above the step's own .ff-top), shows a small circle that pulses
 * during build and turns green on done, plus a click-to-open affordance for
 * the live RunScreen overlay so power users can watch the log without
 * losing their place in the flash flow. */
function TopBanner({ build, onOpenLive }) {
  const pct = Math.round(build.progress);
  const done = build.done;
  return (
    <div className={"ff-topbar" + (done ? " done" : "")} role="status"
      onClick={onOpenLive} title="Open the live build screen">
      <span className={"ff-topbar-dot" + (done ? " g" : "")} />
      <div className="ff-topbar-text">
        <div className="ff-topbar-title">
          {done ? "Your image is ready." : "We're building in the background while you work."}
        </div>
        <div className="ff-topbar-sub">
          <span className="pct">{pct}%</span>
          <span className="sep">·</span>
          <span className="ph">{done ? "system image written" : build.phase}</span>
        </div>
      </div>
      <div className="ff-topbar-track"><i style={{ width: pct + "%" }} /></div>
      <span className="ff-topbar-open">›</span>
    </div>
  );
}

/* The live overlay: full-stage RunScreen with a small "Back to flash setup"
 * pill at the top-right. We re-use RunScreen unchanged, then absolute-overlay
 * the pill on top. RunScreen drives its own internal timer; we pass an
 * onDone that the user typically never hits — they click the pill first. */
function LiveOverlay({ dev, desktop, onBack }) {
  const script = React.useMemo(() => buildScript(dev || { code: "x" }, desktop), [dev && dev.code, desktop]);
  const meta = (
    <span>{(dev && dev.code) || "build"} · <span>live build</span></span>
  );
  return (
    <div className="ff-live">
      <button className="ff-live-back" onClick={onBack} title="Return to flash setup">
        ‹ Back to flash setup
      </button>
      <RunScreen
        script={script}
        title="Building your image…"
        meta={meta}
        phases={BUILD_PHASES}
        eventPrefix="build"
        onDone={onBack}
      />
    </div>
  );
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

/* "Skip the guide" confirmation card. The copy adapts to Samsung's quirk —
 * they need Download Mode, not fastboot, so we call that out by name. */
function SkipCard({ brand }) {
  const isSamsung = brand === "samsung";
  return (
    <div className="ff-skip-card">
      <div className="ff-skip-icon">✓</div>
      <div className="ff-skip-body">
        <div className="ff-skip-h">Great — we'll skip the unlock guide.</div>
        <p className="ff-skip-p">
          Make sure your phone is in <b>{isSamsung ? "download mode" : "fastboot mode"}</b>
          {isSamsung ? " (Samsung's flashing screen)" : ""} and connected, then continue.
        </p>
      </div>
    </div>
  );
}

/* Renders the matched brand's instructions only. The maintainer dropped
 * the accordion-of-all-brands UI — showing four other brands' unlock guides
 * is noise. PinePhone / x86 get the brand's friendly "nothing to do" copy
 * via its `autoConfirm` entry. */
function BrandInstructions({ info }) {
  return (
    <div className="ff-brand">
      <div className="ff-brand-head">
        <span className="ff-brand-pill">your phone</span>
        <h2 className="ff-brand-title">{info.title}</h2>
      </div>
      <p className="ff-brand-blurb">{info.blurb}</p>
      <ol className="ff-brand-steps">
        {info.steps.map((s, i) => <li key={i}>{s}</li>)}
      </ol>
    </div>
  );
}

function StepUnlock({ dev, build, onCancel, onBack, onNext }) {
  const ports = portsFor(dev);
  const brand = ports.brand;
  const info = UNLOCK_BRANDS[brand] || UNLOCK_BRANDS.oppo;
  /* PinePhone + x86 default-confirm since there's nothing for the user to actually do. */
  const [confirmed, setConfirmed] = React.useState(!!info.autoConfirm);
  /* "Already unlocked" skip option — only relevant for brands with a real
   * unlock dance (autoConfirm brands skip already, no point offering it). */
  const canSkip = !info.autoConfirm;
  const [skipUnlock, setSkipUnlock] = React.useState(false);
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
      <div className="ff-body">
        <div className="ff-unlock">
          <p className="ff-lead">
            Phones ship locked — they only run software signed by their maker.
            We need to flip that switch first. This is the slow part of the
            install: most makers add a waiting period. Start it now and let
            it run in the background while we build your image.
          </p>
          {canSkip && (
            <label className={"ff-skip-toggle" + (skipUnlock ? " on" : "")}
              onClick={() => setSkipUnlock(s => !s)}>
              <span className="ff-check">{skipUnlock ? "✓" : ""}</span>
              <span className="ff-skip-toggle-text">
                <b>My bootloader is already unlocked</b>
                <small>Skip the instructions and go straight to connecting my phone.</small>
              </span>
            </label>
          )}
          {skipUnlock
            ? <SkipCard brand={brand} />
            : <BrandInstructions info={info} />
          }
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

/* ===== F3: connect device =============================================== */

/* useMockDetect — in dev mode the fastboot binding is unavailable so we
 * fake a 4-second "scanning" pulse before reporting the device as found.
 * This is the single seam where the real Wails binding will plug in:
 *   useEffect → window.runtime.EventsOn("device:fastboot", setFound)
 *   plus a periodic poll of `App.ListFastbootDevices()`. */
function useMockDetect(dev) {
  const [found, setFound] = React.useState(false);
  React.useEffect(() => {
    setFound(false);
    const t = setTimeout(() => setFound(true), 4000);
    return () => clearTimeout(t);
  }, [dev && dev.code]);
  return found;
}

function StepConnect({ dev, onCancel, onBack, onNext }) {
  const detected = useMockDetect(dev);
  const [helpOpen, setHelpOpen] = React.useState(false);
  const name = (dev && dev.name) || "phone";
  return (
    <div className="ff" data-step="connect">
      <FFTop title="Step 3 of 5 · Plug in your phone" onCancel={onCancel} />
      <div className="ff-body">
        <div className="ff-connect">
          <div className={"ff-cable" + (detected ? " ok" : "")} aria-hidden="true">
            <PK src={HEAD} className="pkw pkgrad" />
            <span className="ff-cable-port" />
            <span className="ff-cable-pulse" />
          </div>
          <h2 className="ff-connect-h2">
            {detected ? "Got it." : "Plug in your phone."}
          </h2>
          <p className="ff-connect-body">
            {detected
              ? `Your computer can see your ${name}. We're good to go.`
              : "Plug your phone into your computer with a USB cable now. Make sure the phone is in the unlock/fastboot screen we set up in the last step."}
          </p>
          <div className={"ff-detect" + (detected ? " ok" : " scan")}>
            {detected ? (
              <React.Fragment>
                <span className="ff-detect-dot" />
                <span className="ff-detect-name">Detected: <b>{name}</b> (fastboot mode)</span>
                <span className="ff-detect-tick">✓</span>
              </React.Fragment>
            ) : (
              <React.Fragment>
                <span className="ff-detect-spin" />
                <span className="ff-detect-name">Looking for a phone…</span>
                <span className="ff-detect-sub">this usually takes a couple of seconds</span>
              </React.Fragment>
            )}
          </div>

          <div className={"ff-help" + (helpOpen ? " open" : "")}>
            <div className="ff-help-head" onClick={() => setHelpOpen(o => !o)}>
              <span className="ff-help-chev">{helpOpen ? "−" : "+"}</span>
              Help, my phone isn't detected
            </div>
            {helpOpen && (
              <div className="ff-help-body">
                <ul>
                  <li><b>Try a different USB cable.</b> Many cheap cables are charge-only and don't carry data.</li>
                  <li><b>Try a different USB port.</b> USB hubs and front-of-tower ports are often flaky — use a port directly on the back of the computer if you can.</li>
                  <li><b>Windows users:</b> install the "Android USB drivers" package, or use Google's "platform-tools". Without these, Windows can't talk to a phone in fastboot mode.</li>
                  <li><b>Make sure the phone is actually in fastboot mode</b> (a tiny text screen, usually black or white, with the word FASTBOOT). If it's powered off, hold Power + Volume Down to enter it.</li>
                  <li><b>Linux users:</b> you may need a udev rule that grants your user access to the device. The app will do this automatically in the production build.</li>
                </ul>
              </div>
            )}
          </div>
        </div>
      </div>
      <FFFoot
        onBack={onBack}
        hint={detected ? "phone found · ready to flash" : "waiting for your phone…"}
        onNext={onNext}
        nextDisabled={!detected}
        nextLabel="Start flashing"
        nextVariant="grad"
      />
    </div>
  );
}

/* ===== F4: live flashing ================================================
 *
 * Three sub-phases: bootloader → recovery → system. Each has its own log
 * script (same shape as buildScript), and the screen-wide progress bar at
 * the top is the combined % across all three. We weight equally — Phase 4
 * later can swap to real fastboot-emitted byte progress when the Wails
 * pipeline ships.
 *
 * Partition convention: we flash to `boot` for the custom second-stage
 * bootloader (minkernel/lk2nd both go there — they replace the stock boot
 * image), `recovery` for PRP, and `system` for the rootfs. This matches
 * what `peacock flash --device <code>` does today.
 *
 * For devices that don't ship a custom bootloader port yet (Pine, x86,
 * Fairphone) the bootloader phase is rendered as a "Not needed for this
 * device — skipping." card and contributes 0% / 0s to the totals. */

const L = (t, prog, node) => ({ t, prog, node });

function bootloaderScript(dev, ports) {
  const img = ports.bootloader;
  if (!img) return null;
  /* minkernel-* and lk2nd-* both flash to the `boot` partition. */
  return [
    L("·",  6, <span>fastboot devices</span>),
    L("·", 14, <span><span className="b">→</span> {dev.code} · fastboot</span>),
    L("·", 22, <span>peacock-resolve <span className="b">{img}</span></span>),
    L("·", 36, <span><span className="g">✓</span> {img}.img <span className="y">(312 KB)</span></span>),
    L("·", 48, <span>fastboot flash boot <span className="b">{img}.img</span></span>),
    L("·", 70, <span>sending <span className="y">'boot'</span> (312 KB)…</span>),
    L("·", 86, <span>writing <span className="y">'boot'</span>…</span>),
    L("·", 100, <span><span className="g">✓</span> boot partition flashed</span>),
  ];
}
function recoveryScript(dev, ports) {
  if (!ports.recovery) return null;
  const img = ports.recovery;
  return [
    L("·",  8, <span>peacock-resolve <span className="b">{img}</span></span>),
    L("·", 22, <span><span className="g">✓</span> {img}.img <span className="y">(8.4 MB)</span></span>),
    L("·", 30, <span>fastboot flash recovery <span className="b">{img}.img</span></span>),
    L("·", 52, <span>sending <span className="y">'recovery'</span> (8.4 MB)…</span>),
    L("·", 78, <span>writing <span className="y">'recovery'</span>…</span>),
    L("·", 100, <span><span className="g">✓</span> recovery partition flashed</span>),
  ];
}
function systemScript(dev) {
  /* The build pipeline writes the rootfs image to ~/.local/var/peacock/<code>.img.
   * We flash it to the `system` partition; userdata is wiped separately. */
  return [
    L("·",  6, <span>peacock-resolve <span className="b">peacockos-{dev.code}.img</span></span>),
    L("·", 14, <span><span className="g">✓</span> rootfs image <span className="y">(1.92 GB)</span></span>),
    L("·", 20, <span>fastboot erase userdata</span>),
    L("·", 28, <span><span className="g">✓</span> userdata erased</span>),
    L("·", 32, <span>fastboot flash system <span className="b">peacockos-{dev.code}.img</span></span>),
    L("·", 50, <span>sending <span className="y">'system'</span> (1.92 GB)…</span>),
    L("·", 72, <span>&nbsp;&nbsp;chunk 12 / 31 · 38.2 MB/s</span>),
    L("·", 88, <span>writing <span className="y">'system'</span>…</span>),
    L("·", 96, <span><span className="g">✓</span> system partition flashed</span>),
    L("·", 100, <span><span className="g">✓</span> all partitions written</span>),
  ];
}

/* Drives one phase to completion using the same setTimeout cadence as
 * RunScreen. onDone fires once we've ticked past the last line. */
function usePhase(script, armed, onDone) {
  const [n, setN] = React.useState(0);
  React.useEffect(() => {
    if (!armed || !script) return;
    if (n >= script.length) {
      const t = setTimeout(onDone, 600);
      return () => clearTimeout(t);
    }
    const t = setTimeout(() => setN(n + 1), n === 0 ? 320 : 360 + Math.random() * 240);
    return () => clearTimeout(t);
  }, [armed, n, script]);
  React.useEffect(() => { if (!armed) setN(0); }, [armed]);
  const prog = !script ? 100 : n > 0 ? script[n - 1].prog : 0;
  return { prog, n, lines: script ? script.slice(0, n) : [] };
}

function StepFlash({ dev, onCancel, onBack, onDone }) {
  const ports = portsFor(dev);
  const blScript = React.useMemo(() => bootloaderScript(dev, ports), [dev.code, ports.bootloader]);
  const rcScript = React.useMemo(() => recoveryScript(dev, ports), [dev.code, ports.recovery]);
  const syScript = React.useMemo(() => systemScript(dev), [dev.code]);

  const [phase, setPhase] = React.useState(blScript ? "bootloader" : (rcScript ? "recovery" : "system"));
  const [finished, setFinished] = React.useState(false);

  /* Sequence: bootloader → recovery → system → reboot. We use one phase
   * hook each, gated by `armed`. A small state machine advances them. */
  const bl = usePhase(blScript, phase === "bootloader", () => setPhase(rcScript ? "recovery" : "system"));
  const rc = usePhase(rcScript, phase === "recovery", () => setPhase("system"));
  const sy = usePhase(syScript, phase === "system", () => setFinished(true));

  /* Combined progress: each phase contributes 1/3. Skipped phases count
   * as 100% so a Pine/x86 flow lands at 33% the moment it starts. */
  const blPct = blScript ? bl.prog : 100;
  const rcPct = rcScript ? rc.prog : 100;
  const syPct = sy.prog;
  const total = Math.round((blPct + rcPct + syPct) / 3);

  /* Auto-advance once flashing finishes, after the "reboot in 10 sec" message. */
  React.useEffect(() => {
    if (!finished) return;
    const t = setTimeout(onDone, 10000);
    return () => clearTimeout(t);
  }, [finished]);

  /* aggregate log: prefix lines with phase tag so the user understands order. */
  const allLines = [
    ...bl.lines.map(l => ({ ...l, _ph: "boot" })),
    ...rc.lines.map(l => ({ ...l, _ph: "recv" })),
    ...sy.lines.map(l => ({ ...l, _ph: "sys"  })),
  ];

  return (
    <div className="ff" data-step="flash">
      <FFTop title="Step 4 of 5 · Flashing PeacockOS" onCancel={onCancel} />
      <div className="ff-flash">
        <div className="ff-flash-top">
          <div className="ff-flash-meta">
            <span>{dev.code}</span>
            <span className="sep">·</span>
            <span>{finished ? "all done" : phase === "bootloader" ? "bootloader" : phase === "recovery" ? "recovery" : "system"}</span>
          </div>
          <div className="ff-flash-pct">{total}<span className="pp">%</span></div>
          <h2 className="ff-flash-h2">
            {finished ? "Almost done" :
             phase === "bootloader" ? "Flashing your custom bootloader…" :
             phase === "recovery"   ? "Flashing the recovery environment…" :
                                       "Flashing PeacockOS…"}
          </h2>
          <p className="ff-flash-sub">
            {finished ? `Your phone will reboot in 10 seconds. The first boot can take a couple of minutes — that's normal.` :
             phase === "bootloader" ? "This is the small program that decides what to boot. PeacockOS needs its own." :
             phase === "recovery"   ? "If anything goes wrong later, you'll boot into this to recover." :
                                       "This is the actual operating system you'll use."}
          </p>
          <div className="ff-flash-track"><i style={{ width: total + "%" }} /></div>
          <div className="ff-flash-phases">
            <PhasePill label="Bootloader" pct={blPct} state={blScript ? (phase === "bootloader" ? "cur" : blPct >= 100 ? "done" : "pend") : "skip"} />
            <PhasePill label="Recovery"   pct={rcPct} state={rcScript ? (phase === "recovery"   ? "cur" : rcPct >= 100 ? "done" : "pend") : "skip"} />
            <PhasePill label="System"     pct={syPct} state={phase === "system" && !finished ? "cur" : finished ? "done" : "pend"} />
          </div>
        </div>
        <div className="ff-flash-log">
          <div className="ff-flash-log-top">live log</div>
          <div className="ff-flash-log-wrap">
            {allLines.length === 0 && <div className="ln dim">starting…</div>}
            {allLines.map((l, i) => (
              <div key={i} className="ln">
                <span className="t">[{l._ph}]</span>
                {l.node}
              </div>
            ))}
            {!finished && <div className="ln"><span className="cur">▍</span></div>}
          </div>
        </div>
      </div>
      <FFFoot
        onBack={onBack}
        hint={finished ? "rebooting in a moment…" : "do not unplug your phone"}
        onNext={onDone}
        nextDisabled={!finished}
        nextLabel="Done"
        nextVariant="grad"
      />
    </div>
  );
}
function PhasePill({ label, pct, state }) {
  return (
    <div className={"ff-fpp " + state}>
      <span className="d" />
      <span className="lab">{label}</span>
      <span className="pc">{state === "skip" ? "skip" : state === "pend" ? "—" : Math.round(pct) + "%"}</span>
    </div>
  );
}

/* ===== F5: done ========================================================= */

function StepDone({ dev, onHome, onBuildAnother }) {
  const name = (dev && dev.name) || "your phone";
  return (
    <div className="ff" data-step="done">
      <div className="ff-done">
        <div className="glow" />
        <PK src={FULL} className="ff-done-pk pkgrad" />
        <div className="ff-done-tag">PEACOCKOS INSTALLED</div>
        <h1 className="ff-done-h1">
          Welcome to PeacockOS, <em>{name}</em>.
        </h1>
        <p className="ff-done-body">
          The hard part is over. {name} now runs PeacockOS.
        </p>
        <div className="ff-done-next">
          <div className="ff-done-next-h">What happens next</div>
          <ol>
            <li><b>Unplug the USB cable</b> when the phone's screen turns on.</li>
            <li><b>Wait 2 to 3 minutes</b> for the first boot. PeacockOS does some
              one-time setup the first time it runs — this is normal.</li>
            <li><b>If it's stuck on the splash for more than 10 minutes</b>, hold
              the power button to force off, then check the troubleshooting page.</li>
          </ol>
        </div>
        <div className="ff-done-acts">
          <Btn variant="grad" ar="→" onClick={onHome}>Done</Btn>
          <Btn variant="ghost" onClick={onBuildAnother}>Build another</Btn>
          <Btn variant="subtle" onClick={onHome}>Show in folder</Btn>
        </div>
      </div>
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
 * State machine: splash → warn → unlock → connect → flash → done. The
 * background build job is armed at mount (splash entry) so it runs in
 * parallel with the user reading the warning + unlock instructions. F0 owns
 * its own splash → dock animation, and F5 has its own celebratory screen. */
export default function FlashFlow({ dev, flavor, initSys, desktop, onHome, appClass }) {
  const [sub, setSub] = React.useState("splash");
  const [discardOpen, setDiscardOpen] = React.useState(false);
  const [liveOpen, setLiveOpen] = React.useState(false);
  const build = useBuildJob(dev, desktop, true); // armed immediately at F0 entry
  const ports = portsFor(dev);

  const cancel = () => setDiscardOpen(true);
  const keep = () => setDiscardOpen(false);
  const discard = () => { setDiscardOpen(false); onHome(); };

  /* Persistent top-docked banner shows on F1..F4. F0 has its own splash
   * (which docks into the same spot), and F5 has its own celebratory layout. */
  const showBanner = sub === "warn" || sub === "unlock" || sub === "connect" || sub === "flash";

  /* Step counter: F0 is a transient pre-step (splash + background build
   * kickoff), not one of the 5 user-actionable substeps. Show just
   * "Preparing" while in F0 so the status bar doesn't say "step 0 / 5". */
  const stepNum = { warn: 1, unlock: 2, connect: 3, flash: 4, done: 5 }[sub];
  const status = (
    <React.Fragment>
      <span className="pd" /><span>{dev ? dev.code : "no device"}</span>
      <span className="sep">·</span><span>flash · {ports.brand}</span>
      <span className="r">
        <span className="crumb">{
          sub === "splash" ? "Preparing" :
          sub === "warn" ? "Warning" :
          sub === "unlock" ? "Unlock" :
          sub === "connect" ? "Connect" :
          sub === "flash" ? "Flashing" : "Done"
        }</span>
        {stepNum && (
          <React.Fragment>
            <span className="sep">·</span>
            <span>step {stepNum} / 5</span>
          </React.Fragment>
        )}
      </span>
    </React.Fragment>
  );

  return (
    <AppShell appClass={appClass} title={<span>Flash to device <span className="dim">· {dev && dev.code}</span></span>} status={status}>
      <div className="ffwrap">
        {showBanner && <TopBanner build={build} onOpenLive={() => setLiveOpen(true)} />}
        {sub === "splash" && <StepSplash onDone={() => setSub("warn")} />}
        {sub === "warn" && <StepWarn dev={dev} onCancel={cancel} onBack={onHome} onNext={() => setSub("unlock")} />}
        {sub === "unlock" && <StepUnlock dev={dev} build={build} onCancel={cancel} onBack={() => setSub("warn")} onNext={() => setSub("connect")} />}
        {sub === "connect" && <StepConnect dev={dev} onCancel={cancel} onBack={() => setSub("unlock")} onNext={() => setSub("flash")} />}
        {sub === "flash" && <StepFlash dev={dev} onCancel={cancel} onBack={() => setSub("connect")} onDone={() => setSub("done")} />}
        {sub === "done" && <StepDone dev={dev} onHome={onHome} onBuildAnother={onHome} />}
        {liveOpen && <LiveOverlay dev={dev} desktop={desktop} onBack={() => setLiveOpen(false)} />}
        <DiscardModal open={discardOpen} onKeep={keep} onDiscard={discard} />
      </div>
    </AppShell>
  );
}
