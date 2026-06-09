/* PackagesStep.jsx — Step 4 of the build wizard.
 *
 * BASIC mode: a curated grid of toggleable app cards. Each card is the
 * name a normal person would recognise ("Firefox", "VLC") plus a one-
 * line plain-English purpose. We keep the underlying arch package names
 * hidden — they're mapped on toggle and written into the shared `pkgs`
 * array so the rest of the wizard / build pipeline doesn't change.
 *
 * ADVANCED mode: same curated cards up top so power users can still
 * cherry-pick, plus the original tag-pill input + suggestion row for
 * raw package names.
 *
 * Shared state contract: `pkgs` is a string[] of package names, e.g.
 * ["firefox-esr", "mpv"]. We never break this shape.
 */
import React from "react";
import { Head } from "./shared.jsx";

/* Curated recommended apps. Each entry maps an app's human name to
 * one or more package names that the build pipeline understands. */
const APPS = [
  { id: "firefox",     name: "Firefox",        blurb: "Web browser you already know",            pkgs: ["firefox-esr"] },
  { id: "libreoffice", name: "LibreOffice",    blurb: "Office documents, spreadsheets, slides",  pkgs: ["libreoffice-still"] },
  { id: "vlc",         name: "VLC",            blurb: "Plays any video or audio file",           pkgs: ["vlc"] },
  { id: "gimp",        name: "GIMP",           blurb: "Image editor, like a free Photoshop",     pkgs: ["gimp"] },
  { id: "thunderbird", name: "Thunderbird",    blurb: "Email client",                            pkgs: ["thunderbird"] },
  { id: "element",     name: "Element",        blurb: "Matrix chat — like Discord but open",    pkgs: ["element-desktop"] },
  { id: "signal",      name: "Signal Desktop", blurb: "Encrypted messaging",                     pkgs: ["signal-desktop"] },
  { id: "inkscape",    name: "Inkscape",       blurb: "Vector drawings, like Illustrator",       pkgs: ["inkscape"] },
  { id: "obs",         name: "OBS Studio",     blurb: "Screen recording + streaming",            pkgs: ["obs-studio"] },
  { id: "mumble",      name: "Mumble",         blurb: "Voice chat for groups",                   pkgs: ["mumble"] },
  { id: "keepassxc",   name: "KeePassXC",      blurb: "Password manager",                        pkgs: ["keepassxc"] },
  { id: "calls",       name: "Calls",          blurb: "Make phone calls (mobile)",               pkgs: ["gnome-calls"] },
  { id: "chatty",      name: "Chatty",         blurb: "SMS app (mobile)",                        pkgs: ["chatty"] },
  { id: "maps",        name: "Maps",           blurb: "Offline maps + navigation",               pkgs: ["gnome-maps"] },
  { id: "angelfish",   name: "Angelfish",      blurb: "Mobile-first browser, lighter than Firefox", pkgs: ["angelfish"] },
];

/* Raw-package suggestion row preserved from the original Packages widget. */
const PKG_SUGGEST = ["firefox-esr", "mpv", "neovim", "foot", "htop", "git", "openssh", "nmap", "calls", "chatty", "gnome-maps", "angelfish"];

/* Flatten APPS package list for "is this app already selected?" check. */
function appIsOn(app, pkgs) {
  return app.pkgs.every(p => pkgs.includes(p));
}

/* Toggle a curated app on/off, mapping to/from underlying package
 * names. Adding is idempotent; removing only strips the packages that
 * the app declared (so a user's manually-typed extras stay). */
function toggleApp(app, pkgs, setPkgs) {
  if (appIsOn(app, pkgs)) {
    setPkgs(pkgs.filter(p => !app.pkgs.includes(p)));
  } else {
    const next = [...pkgs];
    app.pkgs.forEach(p => { if (!next.includes(p)) next.push(p); });
    setPkgs(next);
  }
}

/* The raw tag-pill input + suggestion row from the original BuildFlow.
 * Kept verbatim so advanced users see exactly what they used to. */
function TagInput({ pkgs, setPkgs }) {
  const [val, setVal] = React.useState("");
  const add = (p) => {
    p = p.trim();
    if (p && !pkgs.includes(p)) setPkgs([...pkgs, p]);
    setVal("");
  };
  return (
    <div style={{ maxWidth: 620 }}>
      <div className="chipbox">
        {pkgs.map(p => (
          <span key={p} className="chip">
            {p}
            <span className="x" onClick={() => setPkgs(pkgs.filter(x => x !== p))}>×</span>
          </span>
        ))}
        <label className="chip add">
          <input
            value={val}
            placeholder={pkgs.length ? "add…" : "package name…"}
            onChange={e => setVal(e.target.value)}
            onKeyDown={e => e.key === "Enter" && add(val)}
          />
        </label>
      </div>
      <div className="suggest">
        {PKG_SUGGEST.map(p => (
          <span
            key={p}
            className={"sug" + (pkgs.includes(p) ? " in" : "")}
            onClick={() => add(p)}
          >+ {p}</span>
        ))}
      </div>
    </div>
  );
}

export default function PackagesStep({ pkgs, setPkgs, mode }) {
  const advanced = mode === "advanced";
  const count = APPS.filter(a => appIsOn(a, pkgs)).length;

  return (
    <React.Fragment>
      <Head
        c="STEP 04 / 06 · PACKAGES"
        t={advanced ? "Extra packages" : "What apps would you like?"}
        s={advanced
          ? "Anything beyond the base + desktop set. Pick from the recommended set or type any package name."
          : "Pick the apps you'll actually use. You can always install more later from the terminal."}
      />
      <div className="mbody fade">
        {/* Curated apps: shown in BOTH modes, since advanced users
            should still get to use the friendly grid. */}
        {advanced && (
          <div className="seclbl">From the recommended set:</div>
        )}
        <div className="apps">
          {APPS.map(a => {
            const on = appIsOn(a, pkgs);
            return (
              <div
                key={a.id}
                className={"app" + (on ? " on" : "")}
                onClick={() => toggleApp(a, pkgs, setPkgs)}
              >
                <div className="check">✓</div>
                <div className="an">{a.name}</div>
                <div className="ab">{a.blurb}</div>
              </div>
            );
          })}
        </div>
        {!advanced && (
          <div className="appcount">
            <b>{count}</b> app{count === 1 ? "" : "s"} selected
          </div>
        )}
        {advanced && (
          <React.Fragment>
            <div className="seclbl" style={{ marginTop: 24 }}>Anything else (raw package names):</div>
            <TagInput pkgs={pkgs} setPkgs={setPkgs} />
          </React.Fragment>
        )}
      </div>
    </React.Fragment>
  );
}
