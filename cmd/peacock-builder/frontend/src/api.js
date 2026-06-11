/* api.js — Wails binding shims for the frontend.
 *
 * Strategy:
 *   1. Prefer the generated wailsjs/go/main/App module when Wails has
 *      built it (it lands at frontend/src/wailsjs/go/main/App.js on
 *      `wails build` / `wails dev`). The dynamic import inside a
 *      try/catch means missing-module errors at dev-only-vite time
 *      don't crash the page.
 *   2. Otherwise fall back to window.go.main.App, which is what the
 *      Wails runtime injects at runtime in production builds.
 *   3. Finally, if neither is available (pure `npm run dev` outside
 *      the Wails wrapper), serve stub data so the React tree still
 *      renders and the wizard can be exercised end-to-end.
 *
 * The Run.jsx screen also calls window.runtime.EventsOn("build:log",
 * ...). When there's no Wails runtime we leave that as a noop and the
 * build screen uses its simulated script (Phase 1 behavior). */

import { FALLBACK_DEVICES } from "./devices.js";

const HAS_WAILS_RT = typeof window !== "undefined"
  && window.go && window.go.main && window.go.main.App;

// _appPromise lazily resolves to the generated wailsjs module if it
// exists. We can't `import("./wailsjs/go/main/App.js")` with a literal
// path because Vite/Rollup statically analyzes dynamic imports and
// fails the build when the file is missing (wailsjs/ is .gitignored —
// only the Wails build pipeline writes it). Workarounds:
//   1. Use a Vite virtual import via import.meta.glob — it returns
//      every match (zero or one) without erroring on absence.
//   2. Build the path via concatenation so Rollup can't trace it.
// We pick (1): the glob is empty in plain vite dev, and yields the
// module object once Wails has scaffolded the bindings.
let _appPromise = null;
async function getGeneratedApp() {
  if (_appPromise) return _appPromise;
  _appPromise = (async () => {
    try {
      // import.meta.glob with a wildcard returns a record of matchers
      // — empty {} when no file matches, so the build never fails.
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

// callApp tries the generated module first, then the runtime-injected
// window.go.main.App. Returns null when no binding is available, so
// each public function can decide on its own fallback.
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

export const ListDevices = async () => {
  const real = await callApp("ListDevices");
  if (Array.isArray(real)) return real;
  return FALLBACK_DEVICES;
};

export const RunDoctor = async (opts) => {
  // The Go binding takes (flavor, device, useHostChroot) as positional
  // args, not an object. Map the legacy {flavor, device, useHostChroot}
  // shape the frontend already uses.
  const o = opts || {};
  const real = await callApp("RunDoctor", o.flavor || "", o.device || "", !!o.useHostChroot);
  if (real && typeof real === "object") return real;
  return { summary: { ok: 25, missing: 2, broken: 0, skipped: 0 }, results: [] };
};

export const StartBuild = async (cfg) => {
  // In Wails mode a null/empty result means the binding rejected (e.g.
  // config validation) — surface it so the caller can show build:error
  // instead of sitting at 0% forever. Only fall back to the mock id when
  // there's genuinely no backend (dev preview).
  const real = await callApp("StartBuild", cfg || {});
  if (typeof real === "string" && real) return real;
  if (HAS_WAILS_RT) {
    throw new Error("StartBuild returned no build id — the backend rejected the request");
  }
  return "fake-build-id";
};

export const CancelBuild = async (buildID) => {
  const real = await callApp("CancelBuild", buildID || "");
  return real === true;
};

// StartFlashSet kicks off the bootloader + PRP recovery build for a
// device (the "flashable set"), streaming progress via the
// flashset:log / flashset:phase / flashset:done / flashset:error events.
// Fire-and-forget like StartBuild. In dev mode (no backend) it no-ops;
// the simulated flow handles progress itself.
export const StartFlashSet = async (device) => {
  await callApp("StartFlashSet", device || "");
};

// PrepareFlasher provisions the flash chroot (installs fastboot + heimdall on
// first run). Returns "" on success or an error message. In dev mode (no
// backend) it resolves to "" so the connect step's mock detection runs.
export const PrepareFlasher = async () => {
  const real = await callApp("PrepareFlasher");
  return typeof real === "string" ? real : "";
};

// DetectFlashDevice probes for the device in the mode its flash_method implies.
// Returns an array of detected identifiers (empty = nothing connected yet), or
// null when there's no backend (dev preview) so the caller can fall back to a
// simulated detection.
export const DetectFlashDevice = async (deviceCode) => {
  return await callApp("DetectFlashDevice", deviceCode || "");
};

// PortsStatus reports whether a peacock-ports checkout is present.
// Read-only. In dev mode (no Wails backend) we report present=true so
// the first-time-setup clone screen never appears — the Cloudflare
// preview runs entirely on FALLBACK_DEVICES.
export const PortsStatus = async () => {
  const real = await callApp("PortsStatus");
  if (real && typeof real === "object") return real;
  return { present: true, root: "" };
};

// SyncPorts kicks off the clone (when absent) and streams progress via
// the "ports:log" / "ports:done" / "ports:error" Wails events. In dev
// mode it resolves immediately as present so nothing blocks.
export const SyncPorts = async () => {
  const real = await callApp("SyncPorts");
  if (real && typeof real === "object") return real;
  return { present: true, root: "" };
};
