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
 * Persistent build banner: lives at the top-left of the wizard chrome on
 * F1..F2 (plus F3 while the build is still running). Click it to open the
 * full RunScreen overlay so power users can watch the live log without
 * losing flash-flow state. It unmounts once flashing starts (F4).
 *
 * Phase 1 (today): all jobs are simulated via the same log-script pattern as
 * Run.jsx — see useBuildJob / useFlashJob below. Phase 4 will swap the
 * timers for Wails event subscriptions (window.runtime.EventsOn). */

import React from "react";
import { AppShell, PK, Btn, FULL, HEAD } from "./shared.jsx";
import { buildScript, BUILD_PHASES, FLASHSET_PHASES, ALL_PHASES, RunScreen, useWailsScript } from "./Run.jsx";
import { DEVICE_PORTS, brandOf, brandSlug } from "./devices.js";
import { hasWails } from "./devMock.jsx";
import { StartBuild, StartFlashSet } from "./api.js";

/* buildDTO maps the wizard selections to the BuildRequestDTO the Go
 * StartBuild binding expects (see cmd/peacock-builder/build_runner.go).
 * buildMode drives qemu/cross: "qemu-user" forces qemu, otherwise we let
 * the pipeline auto-resolve (it cross-compiles foreign arches by default
 * when a cross toolchain alias exists for the flavor). */
function buildDTO(cfg) {
  const c = cfg || {};
  return {
    device: (c.dev && c.dev.code) || "",
    flavor: c.flavor || "arch",
    initSystem: c.initSys || "openrc",
    desktop: c.desktop || "none",
    displayManager: c.dm || "none",
    extras: c.pkgs || [],
    userName: "",
    userPassword: "",
    imageSizeMB: 0,
    emptyRootfs: false,
    // "auto" (default) lets each port's use_qemu/cross_compile decide —
    // critical because e.g. the daisy kernel cross-compiles (x86 chroot +
    // aarch64-linux-gnu toolchain) and forcing qemu would try to install
    // that x86 cross toolchain into an aarch64 chroot. Only an explicit
    // qemu-user/native/cross choice overrides the port.
    useQemu: c.buildMode === "qemu-user" ? "true"
      : (c.buildMode === "native" || c.buildMode === "cross") ? "false"
      : "auto",
    crossCompile: "",
    workDir: "",
    architecture: c.arch || (c.dev && c.dev.arch) || "",
  };
}

/* Per-device port wiring (bootloader / recovery / system images) lives
 * in devices.js as DEVICE_PORTS, with `brand` derived there via
 * brandOf(). portsFor() just resolves a device object to its entry,
 * synthesizing a generic one for devices the table doesn't know. */
function portsFor(dev) {
  if (!dev) return DEVICE_PORTS["oppo-a16"];
  return DEVICE_PORTS[dev.id] || DEVICE_PORTS[dev.code] || {
    brand: brandOf(dev.code || dev.id),
    bootloader: null,
    recovery: null,
    system: "linux-" + (dev.code || "device"),
  };
}

/* ===== background build job (F0 kick-off, persistent banner, F2 gate) =====
 *
 * THE single source of truth for build state in this flow. The driver owns
 * exactly one instance and feeds it to both the persistent TopBanner and
 * the full-page live build view (LiveOverlay → RunScreen in controlled
 * mode), so they can never disagree on percent / phase / log / done.
 *
 * Real-Wails mode: useWailsScript("build", …) holds the ONLY set of
 * EventsOn("build:log"/"build:phase"/"build:done"/"build:error")
 * subscriptions; both views are pure renderers of its state.
 *
 * Dev / preview mode (no Wails): drives the same simulated buildScript()
 * lines used in Run.jsx with a local timer. */
function useBuildJob(cfg, armed) {
  const dev = cfg && cfg.dev;
  const desktop = cfg && cfg.desktop;
  // Two event streams under Wails: the flashable set (bootloader + PRP)
  // builds first, then the system image. Both are pure subscriptions;
  // we chain the kickoffs and merge their state into one combined job.
  const fs = useWailsScript("flashset", FLASHSET_PHASES);
  const live = useWailsScript("build", BUILD_PHASES);
  const [n, setN] = React.useState(0);
  const script = React.useMemo(() => buildScript(dev || { code: "x" }, desktop), [dev && dev.code, desktop]);

  /* Kick the flashable-set build first when armed under Wails; when it
   * finishes, kick the system build. Without these the subscriptions sit
   * at 0% forever. Mock mode (no Wails) skips both and animates the
   * simulated script below. Rejections surface via startErr since a
   * never-started job emits no :error event. */
  const fsStartedRef = React.useRef(false);
  const buildStartedRef = React.useRef(false);
  const [startErr, setStartErr] = React.useState("");
  React.useEffect(() => {
    if (!armed || fsStartedRef.current || !hasWails()) return;
    fsStartedRef.current = true;
    StartFlashSet((dev && dev.code) || "").catch((e) => {
      setStartErr(String(e && e.message ? e.message : e));
    });
  }, [armed]);
  const fsDone = fs && fs.done && !fs.errorMsg;
  React.useEffect(() => {
    if (!fsDone || buildStartedRef.current || !hasWails()) return;
    buildStartedRef.current = true;
    StartBuild(buildDTO(cfg)).catch((e) => {
      setStartErr(String(e && e.message ? e.message : e));
    });
  }, [fsDone]);

  React.useEffect(() => {
    if (fs || live || !armed) return;
    if (n >= script.length) return;
    const t = setTimeout(() => setN(n + 1), n === 0 ? 400 : 380 + Math.random() * 300);
    return () => clearTimeout(t);
  }, [fs, live, armed, n, script.length]);

  if (fs || live) {
    // Combined progress: flashable set spans 0-45%, system image 50-100%.
    // The displayed phase is derived from the combined percent against
    // ALL_PHASES so it tracks both stages without depending on matching
    // event-string labels.
    const error = startErr || (fs && fs.errorMsg) || (live && live.errorMsg) || null;
    const lines = [...(fs ? fs.lines : []), ...(live ? live.lines : [])];
    let progress;
    if (!fsDone) {
      progress = (fs ? fs.prog : 0) * 0.45;
    } else {
      progress = 50 + (live ? live.prog : 0) * 0.5;
    }
    const phase = ALL_PHASES.reduce((a, p) => (progress >= p.at ? p.label : a), ALL_PHASES[0].label);
    return {
      progress,
      phase,
      done: live && live.done && !live.errorMsg && !error,
      error,
      lines,
      phaseSet: ALL_PHASES,
    };
  }
  const prog = n > 0 ? script[n - 1].prog : 0;
  const phase = BUILD_PHASES.reduce((a, p) => (prog >= p.at ? p.label : a), BUILD_PHASES[0].label);
  return { progress: prog, phase, done: prog >= 100, error: null, lines: script.slice(0, n), phaseSet: BUILD_PHASES };
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
  const rootRef = React.useRef(null);
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
        /* FLIP measurement: the old CSS used viewport-center math
         * (translate(calc(-50vw + 60px), calc(-50vh + 60px))) which only
         * landed near the dock target at the original window size. Instead,
         * measure where the peacock actually is and where the dock target
         * (an invisible marker at the TopBanner dot's coordinates) actually
         * is, and feed the delta to the CSS transition via custom props.
         * The CSS keeps a viewport-math fallback in case measurement is
         * impossible (element gone mid-frame). Timing is unchanged — the
         * deterministic timer chain below remains the state driver. */
        const root = rootRef.current;
        if (root) {
          const pk = root.querySelector(".ff-splash-pk");
          const tgt = root.querySelector(".ff-dock-target");
          if (pk && tgt) {
            const a = pk.getBoundingClientRect();
            const b = tgt.getBoundingClientRect();
            const dx = (b.left + b.width / 2) - (a.left + a.width / 2);
            const dy = (b.top + b.height / 2) - (a.top + a.height / 2);
            root.style.setProperty("--dock-dx", dx + "px");
            root.style.setProperty("--dock-dy", dy + "px");
            root.style.setProperty("--dock-scale", "0.18");
          }
        }
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
      ref={rootRef}
      className={"ff-splash" + (docking ? " docking" : "")}
      style={{ pointerEvents: "none" }}
      onTransitionEnd={onTransitionEnd}
    >
      {/* Invisible FLIP dock target: sits at the same coordinates as the
        * TopBanner's pulsing dot (which isn't mounted yet during F0). */}
      <span className="ff-dock-target" aria-hidden="true" />
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
 * Rendered by the driver on F1..F2, plus F3 while the build is still
 * running (the gate that matters there). Visually it sits at the wizard's
 * top-left (above the step's own .ff-top), shows a small circle that pulses
 * during build and turns green on done, plus a click-to-open affordance for
 * the live RunScreen overlay so power users can watch the log without
 * losing their place in the flash flow. */
function TopBanner({ build, onOpenLive }) {
  const pct = Math.round(build.progress);
  const done = build.done;
  const failed = !!build.error;
  // Once the image is ready there's nothing to watch — the banner is a
  // passive "done" confirmation, not a link. While building (or on
  // failure, to read the error) it opens the live build screen.
  const navigable = !done;
  return (
    <div className={"ff-topbar" + (done ? " done" : "") + (failed ? " fail" : "") + (navigable ? " nav" : "")} role="status"
      onClick={navigable ? onOpenLive : undefined}
      title={navigable ? "Open the live build screen" : undefined}>
      <span className={"ff-topbar-dot" + (done ? " g" : "") + (failed ? " r" : "")} />
      <div className="ff-topbar-text">
        <div className="ff-topbar-title">
          {failed ? "Build failed — tap for details."
            : done ? "Your image is ready."
            : "We're building in the background while you work."}
        </div>
        <div className="ff-topbar-sub">
          {failed ? (
            <span className="ph">{build.error}</span>
          ) : (
            <React.Fragment>
              <span className="pct">{pct}%</span>
              <span className="sep">·</span>
              <span className="ph">{done ? "system image written" : build.phase}</span>
            </React.Fragment>
          )}
        </div>
      </div>
      <div className="ff-topbar-track"><i style={{ width: (failed ? 100 : pct) + "%" }} /></div>
      {navigable && <span className="ff-topbar-open">›</span>}
    </div>
  );
}

/* The live overlay: full-stage RunScreen with a small "Back to flash setup"
 * pill at the top-right. RunScreen runs in controlled mode — it renders the
 * driver-owned build job (the same object TopBanner reads), so the banner
 * and this full-page view always agree on percent / phase / log / errors.
 * No timer, no subscriptions of its own. Completion routing (auto-advance
 * to F3 when the user is waiting on the build) lives in the driver. */
function LiveOverlay({ dev, build, onBack, onProceed, closing }) {
  const meta = (
    <span>{(dev && dev.code) || "build"} · <span>live build</span></span>
  );
  return (
    <div className={"ff-live" + (closing ? " out" : "")}>
      <button className="ff-live-back" onClick={onBack} title="Return to flash setup">
        ‹ Back to flash setup
      </button>
      <RunScreen
        job={{
          lines: build.lines,
          prog: build.progress,
          phase: build.phase,
          /* RunScreen's `done` just stops the log cursor — include the
           * failure case so an errored build doesn't blink forever. */
          done: build.done || !!build.error,
          errorMsg: build.error,
        }}
        title="Building your image…"
        meta={meta}
        phases={build.phaseSet || BUILD_PHASES}
        onBack={onBack}
        onProceed={onProceed}
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
  pine64: {
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
  "pc-virtual": {
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
  /* ports.brand is the display brand ("OPPO", "Pine64", "PC / virtual");
   * UNLOCK_BRANDS is keyed by its slug form. */
  const brand = brandSlug(ports.brand);
  const info = UNLOCK_BRANDS[brand] || UNLOCK_BRANDS.oppo;
  /* PinePhone + x86 default-confirm since there's nothing for the user to actually do. */
  const [confirmed, setConfirmed] = React.useState(!!info.autoConfirm);
  /* "Already unlocked" skip option — only relevant for brands with a real
   * unlock dance (autoConfirm brands skip already, no point offering it). */
  const canSkip = !info.autoConfirm;
  const [skipUnlock, setSkipUnlock] = React.useState(false);
  /* Continue unlocks as soon as the user has done THEIR part (ticked the
   * unlock confirmation). The build is no longer a gate here: if it's
   * still running, the driver routes Continue to the full-page live build
   * view, which auto-advances to F3 when the build completes. */
  const ready = confirmed;
  const hint = !confirmed
    ? "tick the box below once your phone is unlocked"
    : (build.done
        ? "ready to continue"
        : "still building · " + Math.round(build.progress) + "% — continue to watch it finish");
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

/* After this long without a detection we assume the user is stuck and
 * auto-open the troubleshooting tips (while still scanning). */
const DETECT_SLOW_MS = 30000;

function StepConnect({ dev, onCancel, onBack, onNext }) {
  const detected = useMockDetect(dev);
  const [helpOpen, setHelpOpen] = React.useState(false);
  /* slow: no device after DETECT_SLOW_MS. We never stop scanning — this
   * only adds a reassurance line and pops the help section open so the
   * user knows the app hasn't crashed and what to try. */
  const [slow, setSlow] = React.useState(false);
  React.useEffect(() => {
    if (detected) return;
    const t = setTimeout(() => {
      setSlow(true);
      setHelpOpen(true);
    }, DETECT_SLOW_MS);
    return () => clearTimeout(t);
  }, [detected, dev && dev.code]);
  const name = (dev && dev.name) || "phone";
  return (
    <div className="ff" data-step="connect">
      <FFTop title="Step 3 of 5 · Plug in your phone" onCancel={onCancel} />
      <div className="ff-body">
        <div className="ff-connect">
          <div className={"ff-cable" + (detected ? " ok" : "")} aria-hidden="true">
            <PK src={HEAD} className="pkw pkgrad" />
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
          {slow && !detected && (
            <div className="ff-detect-slow" role="status">
              Still looking… we haven't found your phone yet, but we haven't
              given up either. The tips below fix this for most people.
            </div>
          )}

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
  /* No reset on disarm: phases arm strictly in sequence and never
   * re-arm, so a completed phase keeps its final progress + log lines.
   * (Resetting here made finished phase pills drop to "—" and the
   * combined percentage dip when the next phase took over. Re-entering
   * F4 via Back remounts StepFlash, which resets naturally.) */
  const prog = !script ? 100 : n > 0 ? script[n - 1].prog : 0;
  return { prog, n, lines: script ? script.slice(0, n) : [] };
}

function StepFlash({ dev, onCancel, onBack, onDone, onWriteState }) {
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

  /* Tell the driver whether a destructive write is in flight. From the
   * first log line until the last partition finishes, cancelling can
   * brick the phone — the driver swaps the plain discard modal for a
   * stronger "stop mid-flash?" confirmation while this is true. */
  const writing = allLines.length > 0 && !finished;
  React.useEffect(() => {
    if (onWriteState) onWriteState(writing);
  }, [writing]);
  React.useEffect(() => () => { if (onWriteState) onWriteState(false); }, []);

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

/* ===== stop-mid-flash modal ============================================
 *
 * Shown instead of the discard modal once F4 has started writing to the
 * phone. Interrupting a partition write can brick the device, so the
 * safe action ("Keep flashing") is the primary button and dismissing the
 * backdrop also keeps flashing. */
function StopFlashModal({ open, onKeep, onStop }) {
  if (!open) return null;
  return (
    <div className="ff-modal-wrap" role="dialog">
      <div className="ff-modal-back" onClick={onKeep} />
      <div className="ff-modal">
        <div className="ff-modal-tag">STOP FLASHING</div>
        <h2>Stop mid-flash?</h2>
        <p>Stopping mid-flash can leave your phone unable to boot. Only stop if the flash appears frozen for several minutes.</p>
        <div className="ff-modal-acts">
          <Btn variant="primary" onClick={onKeep}>Keep flashing</Btn>
          <Btn variant="warn" onClick={onStop}>Stop anyway</Btn>
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
export default function FlashFlow({ dev, flavor, initSys, desktop, dm, pkgs, arch, buildMode, onHome, appClass }) {
  const [sub, setSub] = React.useState("splash");
  const [discardOpen, setDiscardOpen] = React.useState(false);
  const [stopOpen, setStopOpen] = React.useState(false);
  const [liveOpen, setLiveOpen] = React.useState(false);
  /* True while the full-page live view is playing its exit animation before
   * unmounting — keeps it mounted so the .ff-live.out fade can run instead of
   * the overlay snapping away. */
  const [liveClosing, setLiveClosing] = React.useState(false);
  const liveCloseTimer = React.useRef(null);
  /* True while F4 is actually writing partitions (first write → last).
   * Cancel behaves differently then: see StopFlashModal above. */
  const [flashWriting, setFlashWriting] = React.useState(false);
  /* True when the user finished F2 (unlock confirmed) before the build
   * did: they're parked on the full-page live build view waiting for it.
   * When the build completes we auto-advance them into F3. Cleared if
   * they back out to F2 instead of waiting. */
  const [waitingOnBuild, setWaitingOnBuild] = React.useState(false);
  // Full wizard config → drives the real StartBuild and the mock script.
  const buildCfg = React.useMemo(
    () => ({ dev, flavor, initSys, desktop, dm, pkgs, arch, buildMode }),
    [dev, flavor, initSys, desktop, dm, pkgs, arch, buildMode]
  );
  const build = useBuildJob(buildCfg, true); // armed immediately at F0 entry
  const ports = portsFor(dev);

  /* F2 → next: build done means straight to F3 (connect); build still
   * running means the user watches it finish on the full-page live view. */
  const unlockNext = () => {
    if (build.done) { setSub("connect"); return; }
    setWaitingOnBuild(true);
    setLiveOpen(true);
  };
  /* Fade the full-page live view out (CSS .ff-live.out) before unmounting it,
   * so returning to / advancing past the wizard step animates instead of
   * snapping. `then` runs after the exit; callers that change `sub` do so
   * BEFORE calling this, so the new step is revealed by the fade rather than
   * flashing the old one underneath. */
  const closeLive = (then) => {
    setLiveClosing(true);
    if (liveCloseTimer.current) clearTimeout(liveCloseTimer.current);
    liveCloseTimer.current = setTimeout(() => {
      setLiveClosing(false);
      setLiveOpen(false);
      setWaitingOnBuild(false);
      if (then) then();
    }, 240);
  };

  const liveBack = () => {
    /* A failed build has nothing to flash — re-entering the wizard steps
     * (warn / unlock / connect) is a dead end, and dropping the user back
     * on the data-loss disclaimer is jarring. Exit to home instead. */
    closeLive(build.error ? onHome : null);
  };

  /* When the user is parked on the live view waiting for the build, we no
   * longer auto-advance on completion — RunScreen shows an explicit
   * "Image built successfully!" panel with a Continue button (onProceed)
   * so the moment is acknowledged, not skipped past. Set the next step first
   * so the fade-out reveals connect, not the unlock step we came from. */
  const proceedFromLive = () => {
    setSub("connect");
    closeLive();
  };

  const cancel = () => (flashWriting ? setStopOpen(true) : setDiscardOpen(true));
  const keep = () => setDiscardOpen(false);
  const discard = () => { setDiscardOpen(false); onHome(); };
  const keepFlashing = () => setStopOpen(false);
  const stopAnyway = () => { setStopOpen(false); onHome(); };

  /* Persistent top-docked banner shows only while the background build is
   * still in progress (or failed) on a pre-flash step — F1 (warn), F2
   * (unlock), F3 (connect). Once the build SUCCEEDS its job is done, so it
   * unmounts rather than lingering as a "your image is ready" bar over the
   * instructions; a failed build (build.done is false) keeps it so the error
   * stays visible. It also unmounts once flashing starts (F4) / the flow
   * finishes (F5), where "system image written" would read like flash
   * progress. F0 has its own splash (which docks into the same spot). */
  const showBanner = !build.done && (sub === "warn" || sub === "unlock" || sub === "connect");

  /* Step counter: F0 is a transient pre-step (splash + background build
   * kickoff), not one of the 5 user-actionable substeps. Show just
   * "Preparing" while in F0 so the status bar doesn't say "step 0 / 5". */
  const stepNum = { warn: 1, unlock: 2, connect: 3, flash: 4, done: 5 }[sub];
  const status = (
    <React.Fragment>
      <span className="pd" /><span>{dev ? dev.name : "no device"}</span>
      {dev && <span className="dimcode">{dev.code}</span>}
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
    <AppShell appClass={appClass} title={<span>Flash to device <span className="dim">· {dev && dev.name}</span> {dev && <span className="dimcode">{dev.code}</span>}</span>} status={status}>
      <div className="ffwrap">
        {showBanner && <TopBanner build={build} onOpenLive={() => setLiveOpen(true)} />}
        {sub === "splash" && <StepSplash onDone={() => setSub("warn")} />}
        {sub === "warn" && <StepWarn dev={dev} onCancel={cancel} onBack={onHome} onNext={() => setSub("unlock")} />}
        {sub === "unlock" && <StepUnlock dev={dev} build={build} onCancel={cancel} onBack={() => setSub("warn")} onNext={unlockNext} />}
        {sub === "connect" && <StepConnect dev={dev} onCancel={cancel} onBack={() => setSub("unlock")} onNext={() => setSub("flash")} />}
        {sub === "flash" && <StepFlash dev={dev} onCancel={cancel} onBack={() => setSub("connect")} onDone={() => setSub("done")} onWriteState={setFlashWriting} />}
        {sub === "done" && <StepDone dev={dev} onHome={onHome} onBuildAnother={onHome} />}
        {(liveOpen || liveClosing) && <LiveOverlay dev={dev} build={build} onBack={liveBack}
          onProceed={waitingOnBuild ? proceedFromLive : null} closing={liveClosing} />}
        <DiscardModal open={discardOpen} onKeep={keep} onDiscard={discard} />
        <StopFlashModal open={stopOpen} onKeep={keepFlashing} onStop={stopAnyway} />
      </div>
    </AppShell>
  );
}
