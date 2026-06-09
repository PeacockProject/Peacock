/* api.js — window.go.* Wails binding shims
 *
 * Phase 1 ships stub implementations matching the eventual
 * wailsjs/go/main/App.js surface. The frontend can import these symbols
 * unconditionally; in dev (no Wails runtime) they resolve to plausible
 * fake data, and Phase 3 swaps the bodies to call window.go.main.App.*
 * once the Go side binds the real methods. */

const HAS_WAILS = typeof window !== "undefined" && window.go && window.go.main && window.go.main.App;

const DEFAULT_DEVICES = [
  { id: "samsung-jflte", name: "Galaxy S4", code: "samsung-jflte", soc: "msm8960", arch: "armv7h", tag: "stable" },
  { id: "xiaomi-daisy", name: "Xiaomi Mi A2 Lite", code: "xiaomi-daisy", soc: "msm8953", arch: "aarch64", tag: "stable" },
  { id: "oppo-a16", name: "OPPO A16", code: "oppo-a16", soc: "mt6765", arch: "aarch64", tag: "testing" },
  { id: "pine-pp", name: "PinePhone", code: "pine64-pinephone", soc: "a64", arch: "aarch64", tag: "stable" },
  { id: "fairphone-fp4", name: "Fairphone 4", code: "fairphone-fp4", soc: "sm7225", arch: "aarch64", tag: "testing" },
  { id: "generic-x86", name: "x86 PC", code: "generic-x86_64", soc: "qemu / uefi", arch: "x86_64", tag: "stable" },
];

export const ListDevices = async () => {
  if (HAS_WAILS && window.go.main.App.ListDevices) return window.go.main.App.ListDevices();
  return DEFAULT_DEVICES;
};

export const RunDoctor = async (opts) => {
  if (HAS_WAILS && window.go.main.App.RunDoctor) return window.go.main.App.RunDoctor(opts || {});
  return { ok: 25, missing: 2, broken: 0, results: [] };
};

export const StartBuild = async (cfg) => {
  if (HAS_WAILS && window.go.main.App.StartBuild) return window.go.main.App.StartBuild(cfg);
  return "fake-build-id";
};
