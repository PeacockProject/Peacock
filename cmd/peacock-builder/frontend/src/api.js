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

// Each device carries an explicit `status` that drives the colored pill
// and tooltip copy on the DevicePickerStep card. Five buckets:
//
//   stable        — green   — Daily-driveable. All major features work.
//   testing       — amber   — Mostly works. Some rough edges. Safe to try.
//   experimental  — orange  — Basic boot works. Many features missing or unstable.
//   partial       — yellow  — Only some features work. Don't use as daily phone.
//   unsupported   — grey    — Port abandoned or never finished. Listed for reference.
//
// `tag` is preserved for backward compat with anything still consuming
// the legacy stable/testing two-bucket field.
const DEFAULT_DEVICES = [
  { id: "samsung-jflte", name: "Galaxy S4", code: "samsung-jflte", soc: "msm8960", arch: "armv7h", tag: "stable", status: "stable" },
  { id: "xiaomi-daisy", name: "Xiaomi Mi A2 Lite", code: "xiaomi-daisy", soc: "msm8953", arch: "aarch64", tag: "stable", status: "stable" },
  { id: "oppo-a16", name: "OPPO A16", code: "oppo-a16", soc: "mt6765", arch: "aarch64", tag: "testing", status: "testing" },
  { id: "pine-pp", name: "PinePhone", code: "pine64-pinephone", soc: "a64", arch: "aarch64", tag: "stable", status: "stable" },
  { id: "fairphone-fp4", name: "Fairphone 4", code: "fairphone-fp4", soc: "sm7225", arch: "aarch64", tag: "testing", status: "testing" },
  { id: "generic-x86", name: "x86 PC", code: "generic-x86_64", soc: "qemu / uefi", arch: "x86_64", tag: "stable", status: "stable" },
];

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
