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

const HAS_WAILS_RT = typeof window !== "undefined"
  && window.go && window.go.main && window.go.main.App;

const DEFAULT_DEVICES = [
  { id: "samsung-jflte", name: "Galaxy S4", code: "samsung-jflte", soc: "msm8960", arch: "armv7h", tag: "stable" },
  { id: "xiaomi-daisy", name: "Xiaomi Mi A2 Lite", code: "xiaomi-daisy", soc: "msm8953", arch: "aarch64", tag: "stable" },
  { id: "oppo-a16", name: "OPPO A16", code: "oppo-a16", soc: "mt6765", arch: "aarch64", tag: "testing" },
  { id: "pine-pp", name: "PinePhone", code: "pine64-pinephone", soc: "a64", arch: "aarch64", tag: "stable" },
  { id: "fairphone-fp4", name: "Fairphone 4", code: "fairphone-fp4", soc: "sm7225", arch: "aarch64", tag: "testing" },
  { id: "generic-x86", name: "x86 PC", code: "generic-x86_64", soc: "qemu / uefi", arch: "x86_64", tag: "stable" },
];

// _appPromise lazily resolves to the generated wailsjs module if it
// exists. We don't import it statically because Vite errors out on
// missing modules at build time, and wailsjs/ is .gitignored — only
// the Wails build pipeline writes it.
let _appPromise = null;
async function getGeneratedApp() {
  if (_appPromise) return _appPromise;
  _appPromise = (async () => {
    try {
      // The path is relative so Vite can resolve it; if the dir is
      // absent the dynamic import throws, we catch and return null.
      // eslint-disable-next-line import/no-unresolved
      const mod = await import("./wailsjs/go/main/App.js");
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
  return DEFAULT_DEVICES;
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
  const real = await callApp("StartBuild", cfg || {});
  if (typeof real === "string" && real) return real;
  return "fake-build-id";
};

export const CancelBuild = async (buildID) => {
  const real = await callApp("CancelBuild", buildID || "");
  return real === true;
};
