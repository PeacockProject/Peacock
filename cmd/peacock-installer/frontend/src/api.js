/* api.js — Wails binding shims for the installer frontend.
 *
 * Strategy mirrors peacock-builder's api.js:
 *   1. Prefer the generated wailsjs/go/main/App module when Wails has
 *      built it (lands at frontend/src/wailsjs/go/main/App.js on
 *      `wails build` / `wails dev`).
 *   2. Otherwise fall back to window.go.main.App, which Wails injects
 *      at runtime in production builds.
 *   3. Finally, if neither is available (pure `npm run dev` outside
 *      the Wails wrapper), serve stub data so the React tree still
 *      renders and the install wizard can be exercised end-to-end.
 *
 * The Run.jsx screen also fires window.runtime.EventsOn for live
 * progress; without the runtime it falls back to its simulated
 * installScript (Phase-1 behavior).
 *
 * Bindings exposed here:
 *   - ListDisks()                — disks_bridge.go
 *   - RunDoctor()                — doctor_bridge.go (no args, smaller probe set)
 *   - StartInstall(req)          — install_runner.go
 *   - CancelInstall(installID)   — app.go
 *   - ListLocaleOptions()        — locale_data.go */

const HAS_WAILS_RT = typeof window !== "undefined"
  && window.go && window.go.main && window.go.main.App;

// DEFAULT_DISKS keeps the mock alive when no backend is wired so a
// plain `npm run dev` session still works.
const DEFAULT_DISKS = [
  { name: "Internal · eMMC", node: "mmcblk0", meta: "samsung-jflte", cap: "16 GB", sizeBytes: 16 * 1024 ** 3, removable: false },
  { name: "microSD", node: "mmcblk1", meta: "removable", cap: "64 GB", sizeBytes: 64 * 1024 ** 3, removable: true },
  { name: "USB-OTG", node: "sda", meta: "SanDisk Ultra", cap: "32 GB", sizeBytes: 32 * 1024 ** 3, removable: true },
];

const DEFAULT_LOCALE = {
  languages: ["English", "Français", "Deutsch", "Español", "日本語", "Nederlands", "Português", "Italiano", "Polski", "简体中文"],
  regions: [
    { tz: "Europe/Amsterdam", off: "UTC+1" },
    { tz: "Europe/London", off: "UTC+0" },
    { tz: "America/New_York", off: "UTC−5" },
    { tz: "America/Los_Angeles", off: "UTC−8" },
    { tz: "Asia/Tokyo", off: "UTC+9" },
    { tz: "Australia/Sydney", off: "UTC+11" },
  ],
  keyboards: [
    { id: "us", name: "English (US)", m: "QWERTY" },
    { id: "uk", name: "English (UK)", m: "QWERTY" },
    { id: "de", name: "German", m: "QWERTZ" },
    { id: "fr", name: "French", m: "AZERTY" },
    { id: "nl", name: "Dutch", m: "QWERTY" },
    { id: "es", name: "Spanish", m: "QWERTY" },
  ],
};

// Lazy-load the generated module. Vite's import.meta.glob with a
// wildcard returns an empty object when nothing matches, so a missing
// wailsjs/ tree doesn't fail the build.
let _appPromise = null;
async function getGeneratedApp() {
  if (_appPromise) return _appPromise;
  _appPromise = (async () => {
    try {
      const modules = import.meta.glob("./wailsjs/go/main/App.js");
      const loader = modules["./wailsjs/go/main/App.js"];
      if (typeof loader !== "function") return null;
      const mod = await loader();
      return mod;
    } catch (_err) {
      return null;
    }
  })();
  return _appPromise;
}

async function callApp(method, ...args) {
  const mod = await getGeneratedApp();
  if (mod && typeof mod[method] === "function") {
    return mod[method](...args);
  }
  if (HAS_WAILS_RT && typeof window.go.main.App[method] === "function") {
    return window.go.main.App[method](...args);
  }
  return null;
}

export const ListDisks = async () => {
  const real = await callApp("ListDisks");
  if (Array.isArray(real)) return real;
  return DEFAULT_DISKS;
};

export const RunDoctor = async () => {
  const real = await callApp("RunDoctor");
  if (real && typeof real === "object") return real;
  return { summary: { ok: 13, missing: 0, broken: 0, skipped: 0 }, results: [] };
};

export const StartInstall = async (req) => {
  const real = await callApp("StartInstall", req || {});
  if (typeof real === "string" && real) return real;
  return "fake-install-id";
};

export const CancelInstall = async (installID) => {
  const real = await callApp("CancelInstall", installID || "");
  return real === true;
};

export const ListLocaleOptions = async () => {
  const real = await callApp("ListLocaleOptions");
  if (real && typeof real === "object") return real;
  return DEFAULT_LOCALE;
};
