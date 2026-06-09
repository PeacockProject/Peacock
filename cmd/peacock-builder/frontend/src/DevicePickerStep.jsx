/* DevicePickerStep.jsx — Step 1 of the build wizard.
 *
 * Picks the device PeacockOS will be flashed to. Features:
 *   1. Search box at the top — live-filters devices by name, codename,
 *      SoC, brand, or arch. `/` focuses it from anywhere.
 *   2. Brand grouping — devices bucketed by codename prefix and
 *      rendered as collapsible sections (open by default).
 *   3. Status pills with hover hints (stable / testing / experimental /
 *      partial / unsupported).
 *   4. Per-card click opens a right-side details drawer ("DPKDrawer")
 *      that shows the device summary + a 2-column "What works" matrix
 *      and a primary "Select this device" CTA. Cards themselves stay
 *      small + uniform; the wizard's footer Continue button still
 *      advances to the next step once a device has been picked. The
 *      drawer hot-swaps content when another card is clicked while it
 *      is open, and closes on Escape, backdrop click, or its own ✕.
 *
 * The support data shown in the matrix is HAND-WRITTEN, intentionally
 * cautious, and will be populated from peacock-ports/device/<name>/
 * device.toml in a future round. The map is keyed by device id and
 * passed in as the `supportMap` prop (defaults to {}). */
import React from "react";
import { Head, Btn } from "./shared.jsx";

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
function brandSlug(dev) {
  return brandOf(dev).toLowerCase().replace(/[^a-z0-9]+/g, "-");
}

/* The 13 standard features displayed in the per-device "What works"
 * matrix. Ordered loosely by how often a user notices breakage. Each
 * device's support map keys these by id; missing keys are treated as
 * "unknown — likely doesn't work yet". */
const FEATURES = [
  { id: "calls",     name: "Calls" },
  { id: "sms",       name: "SMS" },
  { id: "wifi",      name: "WiFi" },
  { id: "bluetooth", name: "Bluetooth" },
  { id: "touch",     name: "Touchscreen" },
  { id: "gpu",       name: "GPU" },
  { id: "battery",   name: "Battery + charging" },
  { id: "audio",     name: "Audio + headphone jack" },
  { id: "camrear",   name: "Camera (rear)" },
  { id: "camfront",  name: "Camera (front)" },
  { id: "gps",       name: "GPS" },
  { id: "sensors",   name: "Sensors (accelerometer, etc.)" },
  { id: "modem",     name: "Modem / mobile data" },
];

// Marks for the three states a feature can be in. "n/a" maps to a
// dimmed dash — used for things like cellular on the qemu / x86 build,
// where the feature simply doesn't apply rather than being broken.
const STATE_MARK = {
  ok:      { icon: "✓", cls: "ok",      label: "Works" },
  partial: { icon: "⚠", cls: "partial", label: "Partial" },
  none:    { icon: "✗", cls: "none",    label: "Doesn't work yet" },
  na:      { icon: "—", cls: "na",      label: "Not applicable" },
};
function featureState(support, fid) {
  const e = support && support[fid];
  if (!e) return { state: "none" };
  if (typeof e === "string") return { state: e };
  return { state: e.state || "none", note: e.note };
}

// Summary line: "11 / 13 work · 2 limited". Skips n/a entries so qemu
// doesn't report "9 / 13" just because cellular is irrelevant.
function summarize(support) {
  let ok = 0, partial = 0, none = 0, total = 0;
  for (const f of FEATURES) {
    const { state } = featureState(support, f.id);
    if (state === "na") continue;
    total++;
    if (state === "ok") ok++;
    else if (state === "partial") partial++;
    else none++;
  }
  return { ok, partial, none, total };
}

/* Drawer prose blurb. The matrix below it has all the technical detail
 * already, so this is a 1–2 sentence "what should I expect" line. If
 * the supportMap entry carries a `_note`, we lead with it. */
function summaryProse(device, support) {
  const sum = summarize(support);
  const note = support && support._note;
  const status = statusOf(device);
  const tail =
    status === "stable"       ? "Daily-driveable on this port — most users won't hit blockers."
    : status === "testing"    ? "Works for most everyday tasks; expect a rough edge here or there."
    : status === "partial"    ? "Boots and runs, but several major features still don't work."
    : status === "unsupported" ? "Listed for reference only — this port isn't actively maintained."
    :                            "Active bring-up: basic boot works, but a lot of hardware isn't wired in yet.";
  const head = note
    ? note
    : `${sum.ok} of ${sum.total} core features work on ${device.name} today${sum.partial ? `, ${sum.partial} partially` : ""}.`;
  return `${head} ${tail}`;
}

function fuzzMatch(dev, q) {
  if (!q) return true;
  const hay = [
    dev.name, dev.code, dev.id, dev.soc, dev.arch, brandOf(dev),
  ].filter(Boolean).join(" ").toLowerCase();
  return hay.includes(q);
}

export default function DevicePickerStep({ devices, dev, onPick, supportMap }) {
  const [query, setQuery] = React.useState("");
  const [collapsedBrands, setCollapsedBrands] = React.useState({});
  // Device whose details the drawer is currently showing. Distinct from
  // `dev` (the wizard's actual picked device): you can peek at one and
  // commit to it (or another) via the drawer's "Select this device" CTA.
  const [drawerId, setDrawerId] = React.useState(null);
  const searchRef = React.useRef(null);
  const sm = supportMap || {};

  // `/` focuses the search box (skip if user is already in another input
  // so we don't fight other shortcuts like B/I). Escape closes the
  // drawer first, falling through to default behaviour otherwise.
  React.useEffect(() => {
    const onKey = (e) => {
      if (e.key === "Escape" && drawerId) {
        e.preventDefault();
        setDrawerId(null);
        return;
      }
      if (e.key !== "/") return;
      const t = e.target;
      if (t && /input|textarea/i.test(t.tagName)) return;
      e.preventDefault();
      if (searchRef.current) searchRef.current.focus();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [drawerId]);

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
  const drawerDevice = drawerId ? devices.find(d => d.id === drawerId) || null : null;
  const openDrawer = (d) => setDrawerId(d.id);
  const closeDrawer = () => setDrawerId(null);
  const selectFromDrawer = (d) => { onPick(d); setDrawerId(null); };

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
                openId={drawerId}
                supportMap={sm}
                onOpen={openDrawer} />
            ))}
          </div>
        )}
      </div>

      <DPKDrawer
        device={drawerDevice}
        support={drawerDevice ? sm[drawerDevice.id] : null}
        selected={dev && drawerDevice && dev.id === drawerDevice.id}
        onClose={closeDrawer}
        onSelect={selectFromDrawer} />
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

function DPKBrandSection({ brand, devices, collapsed, onToggle, selectedId, openId, supportMap, onOpen }) {
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
                open={openId === d.id}
                support={supportMap[d.id]}
                onOpen={() => onOpen(d)} />
            ))}
          </div>
        </div>
      )}
    </section>
  );
}

function DPKCard({ device, selected, open, support, onOpen }) {
  const slug = brandSlug(device);
  const st = statusOf(device);
  const meta = STATUS_META[st];
  const sum = summarize(support);

  // Friendly progress string. Skips zero-noise — e.g. "13 / 13 work"
  // when everything is fine, "11 / 13 work · 2 limited" when partial.
  const progressBits = [`${sum.ok} / ${sum.total} work`];
  if (sum.partial) progressBits.push(`${sum.partial} limited`);
  if (sum.none && !sum.partial) progressBits.push(`${sum.none} missing`);

  return (
    <div className={"dpk-card brand-" + slug + (selected ? " on" : "") + (open ? " peek" : "")}
      onClick={onOpen}
      role="button" tabIndex={0}
      aria-pressed={selected}
      aria-haspopup="dialog"
      aria-expanded={open}
      onKeyDown={e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onOpen(); } }}>
      <div className="dpk-card-accent" aria-hidden="true" />
      <div className={"dpk-pill dpk-stat-" + st} title={meta.hint}>{meta.label}</div>
      <div className="dpk-card-name">{device.name}</div>
      <div className="dpk-card-code">{device.code}</div>
      <div className="dpk-card-soc">{device.soc} · {device.arch}</div>
      <div className="dpk-card-prog">{progressBits.join(" · ")}</div>
    </div>
  );
}

/* ----------------------------------------------------------------------
 * DPKDrawer — right-side slide-in panel with device detail.
 *
 * Stays mounted while a device is "peeked at" (drawerId !== null in the
 * parent). Animates via the .open class toggling translateX 100% → 0
 * over 220 ms. The backdrop dims the picker and absorbs clicks so the
 * user can't accidentally re-trigger a card underneath the dim.
 * -------------------------------------------------------------------- */
function DPKDrawer({ device, support, selected, onClose, onSelect }) {
  const closeBtnRef = React.useRef(null);
  const rootRef = React.useRef(null);
  // Keep the LAST device around for one render after `device` goes null
  // so the slide-out animation has something to paint. Once the CSS
  // transition would have finished (~240 ms) we clear it.
  const [last, setLast] = React.useState(device);
  React.useEffect(() => {
    if (device) {
      setLast(device);
      return;
    }
    const t = setTimeout(() => setLast(null), 260);
    return () => clearTimeout(t);
  }, [device]);

  // Auto-focus the close button when the drawer opens so Tab cycles
  // ✕ → summary link area → matrix items → Cancel → Select, then wraps.
  React.useEffect(() => {
    if (device && closeBtnRef.current) {
      const id = requestAnimationFrame(() => {
        if (closeBtnRef.current) closeBtnRef.current.focus();
      });
      return () => cancelAnimationFrame(id);
    }
  }, [device && device.id]);

  const shown = device || last;
  if (!shown) return null;
  const open = !!device;
  const slug = brandSlug(shown);
  const st = statusOf(shown);
  const meta = STATUS_META[st];
  const prose = summaryProse(shown, support);
  const sum = summarize(support);

  return (
    <div className={"dpk-drawer-root" + (open ? " open" : " closing")}
      ref={rootRef}
      aria-hidden={!open}>
      <div className="dpk-drawer-backdrop" onClick={onClose} />
      <aside className={"dpk-drawer brand-" + slug}
        role="dialog" aria-modal="true"
        aria-label={`Details for ${shown.name}`}>
        <div className="dpk-drawer-accent" aria-hidden="true" />
        <header className="dpk-drawer-head">
          <div className="dpk-drawer-titles">
            <div className="dpk-drawer-name">{shown.name}</div>
            <div className={"dpk-pill dpk-stat-" + st} title={meta.hint}>{meta.label}</div>
          </div>
          <button type="button" className="dpk-drawer-x"
            ref={closeBtnRef}
            onClick={onClose}
            aria-label="Close details">✕</button>
        </header>

        <div className="dpk-drawer-meta">
          <span><b>{brandOf(shown)}</b></span>
          <span className="dpk-drawer-sep">·</span>
          <span className="dpk-drawer-mono">{shown.code}</span>
          <span className="dpk-drawer-sep">·</span>
          <span className="dpk-drawer-mono">{shown.soc}</span>
          <span className="dpk-drawer-sep">·</span>
          <span className="dpk-drawer-mono">{shown.arch}</span>
        </div>

        <div className="dpk-drawer-body">
          <p className="dpk-drawer-prose" tabIndex={0}>{prose}</p>

          <div className="dpk-drawer-progress">
            <span className="dpk-drawer-progress-num">{sum.ok}<small>/{sum.total}</small></span>
            <span className="dpk-drawer-progress-lab">core features working
              {sum.partial ? <small> · {sum.partial} partial</small> : null}
              {sum.none && !sum.partial ? <small> · {sum.none} missing</small> : null}
            </span>
          </div>

          <div className="dpk-drawer-mxhead">What works</div>
          <DPKMatrix support={support} />
        </div>

        <div className="dpk-drawer-foot">
          <FocusBtn onClick={onClose} ariaLabel="Cancel and close drawer">
            <Btn variant="ghost" onClick={onClose}>Cancel</Btn>
          </FocusBtn>
          <div className="dpk-drawer-foot-sp" />
          <FocusBtn onClick={() => onSelect(shown)} ariaLabel="Select this device and continue">
            <Btn variant="primary" ar="→" onClick={() => onSelect(shown)}>
              {selected ? "Keep this device" : "Select this device"}
            </Btn>
          </FocusBtn>
        </div>
      </aside>
    </div>
  );
}

/* The shared <Btn /> atom renders as a non-focusable <div>, which would
 * leave Cancel / Select unreachable via Tab. FocusBtn is a thin role
 * wrapper that makes them keyboard-activatable without forking shared.jsx. */
function FocusBtn({ children, onClick, ariaLabel }) {
  return (
    <span className="dpk-drawer-fbtn"
      role="button" tabIndex={0}
      aria-label={ariaLabel}
      onKeyDown={e => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          onClick && onClick();
        }
      }}>
      {children}
    </span>
  );
}

/* Two-column "What works / What doesn't" grid. Left column lists every
 * feature with state "ok"; right column lists the rest (partial + none),
 * each annotated with its mark and optional one-line note. */
function DPKMatrix({ support }) {
  const works = [];
  const broken = [];
  for (const f of FEATURES) {
    const { state, note } = featureState(support, f.id);
    const row = { ...f, state, note };
    if (state === "ok") works.push(row);
    else if (state === "na") { /* skip — feature doesn't apply to this device */ }
    else broken.push(row);
  }
  return (
    <div className="dpk-matrix" onClick={e => e.stopPropagation()}>
      <div className="dpk-mx-col">
        <div className="dpk-mx-hd dpk-mx-hd-ok">
          <span className="dpk-mx-ic">✓</span> Works
        </div>
        <ul className="dpk-mx-list">
          {works.length === 0
            ? <li className="dpk-mx-row dpk-mx-empty">Nothing confirmed yet.</li>
            : works.map(r => (
              <li key={r.id} className="dpk-mx-row dpk-mx-ok" tabIndex={0}>
                <span className="dpk-mx-rk">✓</span>
                <span className="dpk-mx-rn">{r.name}</span>
              </li>
            ))}
        </ul>
      </div>
      <div className="dpk-mx-col">
        <div className="dpk-mx-hd dpk-mx-hd-bad">
          <span className="dpk-mx-ic">✗</span> Doesn't work yet
        </div>
        <ul className="dpk-mx-list">
          {broken.length === 0
            ? <li className="dpk-mx-row dpk-mx-empty">Nothing missing — everything tested works.</li>
            : broken.map(r => (
              <li key={r.id} className={"dpk-mx-row dpk-mx-" + STATE_MARK[r.state].cls} tabIndex={0}>
                <span className="dpk-mx-rk">{STATE_MARK[r.state].icon}</span>
                <span className="dpk-mx-rn">
                  {r.name}
                  {r.note ? <small className="dpk-mx-note"> · {r.note}</small> : null}
                </span>
              </li>
            ))}
        </ul>
      </div>
    </div>
  );
}
