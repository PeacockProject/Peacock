/* devices.js — single source of truth for device-related static data.
 *
 * Everything the frontend "knows" about devices without asking the Go
 * backend lives here: the dev-mode fallback device list, brand
 * derivation from codenames, the per-device port wiring used by the
 * flash flow, and the hand-written feature-support matrix.
 *
 * Adding a device means touching THIS file (plus, eventually, the real
 * peacock-ports/device/<name>/device.toml that will replace these
 * stubs). Nothing else should hardcode device facts. */

/* ===== fallback device list ==============================================
 *
 * Served by api.js's ListDevices() shim when no Wails binding is
 * available (plain `npm run dev` / Cloudflare preview). Display names
 * are the devices' REAL retail names — e.g. xiaomi-daisy is the
 * "Xiaomi Mi A2 Lite" (the Redmi 6A is codename cactus, a different
 * port).
 *
 * Each device carries an explicit `status` that drives the colored pill
 * and tooltip copy on the DevicePickerStep card. Five buckets:
 *
 *   stable        — green   — Daily-driveable. All major features work.
 *   testing       — amber   — Mostly works. Some rough edges. Safe to try.
 *   experimental  — orange  — Basic boot works. Many features missing or unstable.
 *   partial       — yellow  — Only some features work. Don't use as daily phone.
 *   unsupported   — grey    — Port abandoned or never finished. Listed for reference.
 *
 * `tag` is preserved for backward compat with anything still consuming
 * the legacy stable/testing two-bucket field. */
export const FALLBACK_DEVICES = [
  { id: "samsung-jflte", name: "Galaxy S4", code: "samsung-jflte", soc: "msm8960", arch: "armv7h", tag: "stable", status: "stable" },
  { id: "xiaomi-daisy", name: "Xiaomi Mi A2 Lite", code: "xiaomi-daisy", soc: "msm8953", arch: "aarch64", tag: "stable", status: "stable" },
  { id: "oppo-a16", name: "OPPO A16", code: "oppo-a16", soc: "mt6765", arch: "aarch64", tag: "testing", status: "testing" },
  { id: "pine-pp", name: "PinePhone", code: "pine64-pinephone", soc: "a64", arch: "aarch64", tag: "stable", status: "stable" },
  { id: "fairphone-fp4", name: "Fairphone 4", code: "fairphone-fp4", soc: "sm7225", arch: "aarch64", tag: "testing", status: "testing" },
  { id: "generic-x86", name: "x86 PC", code: "generic-x86_64", soc: "qemu / uefi", arch: "x86_64", tag: "stable", status: "stable" },
];

/* ===== brand derivation ==================================================
 *
 * The ONE place a codename prefix maps to a brand. Takes a codename (or
 * device id — both share the same prefixes) and returns the display
 * brand. Callers that need a CSS-class / lookup-key form run the result
 * through brandSlug(). */
export function brandOf(code) {
  const c = (code || "").toLowerCase();
  if (c.startsWith("samsung-")) return "Samsung";
  if (c.startsWith("xiaomi-")) return "Xiaomi";
  if (c.startsWith("oppo-")) return "OPPO";
  if (c.startsWith("pine-") || c.startsWith("pine64-")) return "Pine64";
  if (c.startsWith("fairphone-")) return "Fairphone";
  if (c.startsWith("generic-x86") || c.startsWith("qemu-")) return "PC / virtual";
  return "Other";
}

/* brandSlug — display brand → css/key slug ("PC / virtual" → "pc-virtual"). */
export function brandSlug(brand) {
  return (brand || "").toLowerCase().replace(/[^a-z0-9]+/g, "-");
}

/* ===== per-device port wiring ============================================
 *
 * Mirrors peacock-ports/device/. Three independent flags so the flash
 * flow can render "skip this phase" for devices that don't have the
 * asset yet.
 *   bootloader: which custom bootloader image is flashed first (or null).
 *   recovery:   PRP recovery ramdisk port (all supported devices have one).
 *   system:     the actual rootfs/system image baked by the build job.
 *
 * For the bootloader: OPPO/MTK uses minkernel (a tiny preloader stub),
 * Snapdragon devices use lk2nd (the community little-kernel fork). x86
 * and PinePhone have no second-stage bootloader to flash here.
 * Fairphone 4 is listed as "TBD" because the port hasn't landed yet —
 * we mock-skip it.
 *
 * `brand` is NOT hand-written per entry — it's derived from the device
 * id via brandOf(), so brand knowledge lives in exactly one function. */
const PORT_DATA = {
  "oppo-a16":      { bootloader: "minkernel-oppo-a16",  recovery: "prp-oppo-a16",      system: "linux-oppo-a16" },
  "xiaomi-daisy":  { bootloader: "lk2nd-xiaomi-daisy",  recovery: "prp-xiaomi-daisy",  system: "linux-xiaomi-daisy-prp" },
  "samsung-jflte": { bootloader: "lk2nd-samsung-jflte", recovery: "prp-samsung-jflte", system: "linux-samsung-jflte" },
  "pine-pp":       { bootloader: null,                  recovery: null,                system: "linux-pinephone" },
  "fairphone-fp4": { bootloader: null,                  recovery: null,                system: "linux-fairphone-fp4" },
  "generic-x86":   { bootloader: null,                  recovery: null,                system: "linux-generic-x86" },
};

export const DEVICE_PORTS = Object.fromEntries(
  Object.entries(PORT_DATA).map(([id, port]) => [id, { brand: brandOf(id), ...port }])
);

/* ===== per-device feature support ========================================
 *
 * Shown in the DevicePickerStep "What works / What doesn't" matrix.
 *
 * Shape:
 *   {
 *     "<device-id>": {
 *       _note: string,                // optional, surfaced in the UI
 *       <feature-id>: "ok"            // shorthand
 *         | "partial"
 *         | "none"
 *         | "na"                      // feature doesn't apply
 *         | { state, note },          // long form with one-line caveat
 *       ...
 *     }
 *   }
 *
 * Feature ids match FEATURES in DevicePickerStep.jsx (calls, sms, wifi,
 * bluetooth, touch, gpu, battery, audio, camrear, camfront, gps,
 * sensors, modem). Missing keys are treated as "none / unknown".
 *
 * Everything here is HAND-WRITTEN and intentionally cautious. We err on
 * the side of marking borderline features as "partial" so users don't
 * get surprised by a feature that mostly works but flakes out under
 * load. The real backend will populate this from
 * peacock-ports/device/<name>/device.toml in a future round and this
 * stub will go away. */
export const DEFAULT_DEVICE_SUPPORT = {
  // Galaxy S4 — old phone, well-trodden port. Daily phone for a few users.
  "samsung-jflte": {
    _note: "Old phone, but reliable port. Battery life is decent.",
    calls: "ok",
    sms: "ok",
    wifi: "ok",
    bluetooth: "ok",
    touch: "ok",
    gpu: "ok",
    battery: "ok",
    audio: "ok",
    camrear: "ok",
    camfront: "none",
    gps: "ok",
    sensors: "ok",
    modem: { state: "partial", note: "Cellular data works but VoLTE doesn't yet." },
  },
  // Mi A2 Lite — stable port; mid-feature parity.
  "xiaomi-daisy": {
    calls: "ok",
    sms: "ok",
    wifi: "ok",
    bluetooth: "ok",
    touch: "ok",
    gpu: "ok",
    battery: "ok",
    audio: "ok",
    camrear: { state: "partial", note: "Photos work, video capture is unstable." },
    camfront: { state: "partial", note: "Stills only — preview hangs sometimes." },
    gps: "ok",
    sensors: "none",
    modem: { state: "partial", note: "2G/3G/4G data ok; SMS over LTE is patchy." },
  },
  // OPPO A16 — active bring-up. Touch just got fixed this week.
  "oppo-a16": {
    _note: "Active development — improving quickly.",
    calls: "none",
    sms: "none",
    wifi: { state: "partial", note: "Connects, but reconnect after sleep is flaky." },
    bluetooth: "none",
    touch: "ok",
    gpu: "ok",
    battery: { state: "partial", note: "Reads charge level; charging detection is rough." },
    audio: "none",
    camrear: "none",
    camfront: "none",
    gps: "none",
    sensors: { state: "partial", note: "Accelerometer reports; magnetometer doesn't." },
    modem: "none",
  },
  // PinePhone — strong mainline support, daily-driveable for many.
  "pine-pp": {
    calls: "ok",
    sms: "ok",
    wifi: "ok",
    bluetooth: "ok",
    touch: "ok",
    gpu: "ok",
    battery: "ok",
    audio: "ok",
    camrear: { state: "partial", note: "Stills ok; autofocus and HDR aren't wired up." },
    camfront: { state: "partial", note: "Works for video calls; low-light is rough." },
    gps: "ok",
    sensors: "ok",
    modem: "ok",
  },
  // Fairphone 4 — recent port, basic boot is in.
  "fairphone-fp4": {
    calls: { state: "partial", note: "Outgoing works; some carriers reject incoming." },
    sms: { state: "partial", note: "Send works, receive is unreliable." },
    wifi: "ok",
    bluetooth: { state: "partial", note: "Pairing works; A2DP audio cuts out." },
    touch: "ok",
    gpu: { state: "partial", note: "Hardware accel works; some compositors stutter." },
    battery: "ok",
    audio: { state: "partial", note: "Speaker ok; headphone-jack switch is flaky." },
    camrear: "none",
    camfront: "none",
    gps: { state: "partial", note: "Cold fix takes a long time." },
    sensors: { state: "partial", note: "Accelerometer + light ok; rest not wired." },
    modem: { state: "partial", note: "Data works; no VoLTE." },
  },
  // generic-x86 — qemu VM. Many features just don't apply.
  "generic-x86": {
    _note: "VM target — features that need cellular hardware don't apply.",
    calls: "na",
    sms: "na",
    wifi: "ok",
    bluetooth: "ok",
    touch: "ok",
    gpu: "ok",
    battery: "ok",
    audio: { state: "partial", note: "Depends on host — PulseAudio passthrough works, raw ALSA varies." },
    camrear: "na",
    camfront: "na",
    gps: "na",
    sensors: "ok",
    modem: "na",
  },
};
