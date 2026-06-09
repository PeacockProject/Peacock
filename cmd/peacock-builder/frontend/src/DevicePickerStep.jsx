/* DevicePickerStep.jsx — Step 1 of the build wizard.
 *
 * Picks the device PeacockOS will be flashed to. Features in this first
 * pass:
 *   1. Search box at the top — live-filters devices by name, codename,
 *      SoC, brand, or arch. `/` focuses it from anywhere.
 *   2. Brand grouping — devices bucketed by codename prefix and
 *      rendered as collapsible sections (open by default).
 *
 * The selection behavior matches the old inline grid: a single click on
 * the card surface advances the wizard (handled by onPick in BuildFlow). */
import React from "react";
import { Head } from "./shared.jsx";

/* Brand inference from codename. Order of BRAND_ORDER below also drives
 * the section render order — most-tested brands first. */
const BRAND_ORDER = ["OPPO", "Xiaomi", "Samsung", "Pine64", "Fairphone", "PC / virtual", "Other"];

/* Five status buckets. Each maps to a colored pill on the device card
 * and an explanatory tooltip. Real backend will populate `status` per
 * device from peacock-ports in a future round — for now the stub data
 * in api.js hand-codes them. */
const STATUS_META = {
  stable:       { label: "Stable",       hint: "Daily-driveable. All major features work." },
  testing:      { label: "Testing",      hint: "Mostly works. Some rough edges. Safe to try." },
  experimental: { label: "Experimental", hint: "Basic boot works. Many features missing or unstable." },
  partial:      { label: "Partial",      hint: "Only some features work. Don't use as daily phone." },
  unsupported:  { label: "Unsupported",  hint: "Port abandoned or never finished. Listed for reference." },
};
function statusOf(dev) {
  const s = (dev.status || dev.tag || "").toLowerCase();
  return STATUS_META[s] ? s : "experimental";
}
function brandOf(dev) {
  const c = (dev.code || dev.id || "").toLowerCase();
  if (c.startsWith("samsung-")) return "Samsung";
  if (c.startsWith("xiaomi-")) return "Xiaomi";
  if (c.startsWith("oppo-")) return "OPPO";
  if (c.startsWith("pine-") || c.startsWith("pine64-")) return "Pine64";
  if (c.startsWith("fairphone-")) return "Fairphone";
  if (c.startsWith("generic-x86") || c.startsWith("qemu-")) return "PC / virtual";
  return "Other";
}

function fuzzMatch(dev, q) {
  if (!q) return true;
  const hay = [
    dev.name, dev.code, dev.id, dev.soc, dev.arch, brandOf(dev),
  ].filter(Boolean).join(" ").toLowerCase();
  return hay.includes(q);
}

export default function DevicePickerStep({ devices, dev, onPick }) {
  const [query, setQuery] = React.useState("");
  const [collapsedBrands, setCollapsedBrands] = React.useState({});
  const searchRef = React.useRef(null);

  // `/` focuses the search box (skip if user is already in another input
  // so we don't fight other shortcuts like B/I).
  React.useEffect(() => {
    const onKey = (e) => {
      if (e.key !== "/") return;
      const t = e.target;
      if (t && /input|textarea/i.test(t.tagName)) return;
      e.preventDefault();
      if (searchRef.current) searchRef.current.focus();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  const q = query.trim().toLowerCase();
  const filtered = React.useMemo(() => devices.filter(d => fuzzMatch(d, q)), [devices, q]);

  // Bucket filtered devices by brand. Brands with zero matches don't
  // render at all (per spec — empty sections collapse out of view).
  const groups = React.useMemo(() => {
    const m = new Map();
    for (const d of filtered) {
      const b = brandOf(d);
      if (!m.has(b)) m.set(b, []);
      m.get(b).push(d);
    }
    const ordered = [];
    for (const b of BRAND_ORDER) if (m.has(b)) ordered.push([b, m.get(b)]);
    for (const [b, ds] of m) if (!BRAND_ORDER.includes(b)) ordered.push([b, ds]);
    return ordered;
  }, [filtered]);

  const totalShown = filtered.length;
  const totalAll = devices.length;

  return (
    <React.Fragment>
      <Head c="STEP 01 / 06 · TARGET" t="Choose a device"
        s="Pick the phone or computer this image will be flashed to. The chip and bootloader are read from the device profile." />
      <div className="mbody fade">
        <div className="dpk-toolbar">
          <div className="dpk-search">
            <span className="dpk-search-ic" aria-hidden="true">⌕</span>
            <input
              ref={searchRef}
              className="dpk-search-inp"
              type="text"
              placeholder="Search devices — name, codename, chip…"
              value={query}
              onChange={e => setQuery(e.target.value)}
              aria-label="Search devices" />
            <span className="dpk-search-kbd" aria-hidden="true">/</span>
          </div>
          <div className="dpk-count">
            {q
              ? <span><b>{totalShown}</b> of {totalAll} {totalAll === 1 ? "device" : "devices"}</span>
              : <span><b>{totalAll}</b> {totalAll === 1 ? "device" : "devices"}</span>}
          </div>
        </div>

        {totalShown === 0 ? (
          <DPKEmpty query={query} />
        ) : (
          <div className="dpk-groups">
            {groups.map(([brand, ds]) => (
              <DPKBrandSection
                key={brand}
                brand={brand}
                devices={ds}
                collapsed={!!collapsedBrands[brand]}
                onToggle={() => setCollapsedBrands(cb => ({ ...cb, [brand]: !cb[brand] }))}
                selectedId={dev && dev.id}
                onPick={onPick} />
            ))}
          </div>
        )}
      </div>
    </React.Fragment>
  );
}

function DPKEmpty({ query }) {
  return (
    <div className="dpk-empty">
      <div className="dpk-empty-ic" aria-hidden="true">⌕</div>
      <div className="dpk-empty-h">No devices matched <i>“{query}”</i>.</div>
      <p className="dpk-empty-p">
        We test a small set of phones at a time — if you don’t see yours,
        ports for new devices are tracked at{" "}
        <a className="dpk-empty-a"
          href="https://github.com/PeacockProject/peacock-ports"
          target="_blank" rel="noreferrer noopener">
          github.com/PeacockProject/peacock-ports
        </a>.
      </p>
    </div>
  );
}

function DPKBrandSection({ brand, devices, collapsed, onToggle, selectedId, onPick }) {
  return (
    <section className={"dpk-grp" + (collapsed ? " collapsed" : "")}>
      <header className="dpk-grp-head" onClick={onToggle} role="button" tabIndex={0}
        onKeyDown={e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onToggle(); } }}>
        <span className="dpk-grp-chev" aria-hidden="true">{collapsed ? "▸" : "▾"}</span>
        <span className="dpk-grp-name">{brand}</span>
        <span className="dpk-grp-count">({devices.length})</span>
      </header>
      {!collapsed && (
        <div className="dpk-grp-body">
          <div className="dpk-cards">
            {devices.map(d => (
              <DPKCard
                key={d.id}
                device={d}
                selected={selectedId === d.id}
                onPick={() => onPick(d)} />
            ))}
          </div>
        </div>
      )}
    </section>
  );
}

function DPKCard({ device, selected, onPick }) {
  const brandSlug = brandOf(device).toLowerCase().replace(/[^a-z0-9]+/g, "-");
  const st = statusOf(device);
  const meta = STATUS_META[st];
  return (
    <div className={"dpk-card brand-" + brandSlug + (selected ? " on" : "")}
      onClick={onPick}
      role="button" tabIndex={0}
      onKeyDown={e => { if (e.key === "Enter") { e.preventDefault(); onPick(); } }}>
      <div className="dpk-card-accent" aria-hidden="true" />
      <div className={"dpk-pill dpk-stat-" + st} title={meta.hint}>{meta.label}</div>
      <div className="dpk-card-name">{device.name}</div>
      <div className="dpk-card-code">{device.code}</div>
      <div className="dpk-card-soc">{device.soc} · {device.arch}</div>
    </div>
  );
}
