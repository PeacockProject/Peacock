/* DesktopStep.jsx — Step 3 of the build wizard.
 *
 * In BASIC mode (default), we drop the words "desktop environment" and
 * "display manager" entirely. The user sees six big cards explaining
 * — in plain English — what their phone will LOOK like after boot.
 * Each card has a tiny CSS wireframe so a brand-new Linux user can
 * recognise the shape (phone home screen vs tablet vs minimal status line).
 *
 * Smart DM defaults: the display manager is chosen for the user based
 * on the desktop they picked. The DM row is hidden in basic mode.
 *
 * In ADVANCED mode, we keep the original 6-card row with the terse
 * subtitles and the explicit DM segmented control.
 */
import React from "react";
import { Head, Field, Seg } from "./shared.jsx";

/* Display managers list — same as the original BuildFlow. */
const DMS = ["none", "sddm", "greetd", "lightdm"];

/* Smart DM defaults per desktop. Used in basic mode so the user
 * doesn't have to pick a "greeter" they've never heard of. */
const DM_DEFAULTS = {
  none: "none",
  phosh: "sddm",
  "plasma-mobile": "sddm",
  sxmo: "none",
  gnome: "sddm",
  weston: "none",
};

/* The six desktops, each with both a friendly (basic) and terse
 * (advanced) presentation. `illus` names an inline CSS wireframe
 * defined in Illus() below. */
const DESKTOPS = [
  {
    id: "none",
    illus: "none",
    advTitle: "None",
    advSub: "console only",
    title: "No desktop · console only",
    blurb: "Just a black screen with a text login. Best if you only need SSH or a server, not a phone you carry around.",
  },
  {
    id: "phosh",
    illus: "phosh",
    advTitle: "Phosh",
    advSub: "GTK · GNOME mobile",
    title: "Phone-style (Phosh)",
    blurb: "Looks like a normal smartphone. Swipe up for the app drawer, top bar for status. Best for phones used as phones.",
  },
  {
    id: "plasma-mobile",
    illus: "plasma",
    advTitle: "Plasma",
    advSub: "Qt · KDE mobile",
    title: "Tablet-friendly (Plasma Mobile)",
    blurb: "More like a tablet computer. Bigger touch targets, can run normal desktop apps in windows.",
  },
  {
    id: "sxmo",
    illus: "sxmo",
    advTitle: "Sxmo",
    advSub: "suckless · minimal",
    title: "Power user (Sxmo)",
    blurb: "Super minimal. No menus, just keys and a status line. Lightest on battery, runs on potato hardware.",
  },
  {
    id: "gnome",
    illus: "gnome",
    advTitle: "GNOME",
    advSub: "adaptive desktop",
    title: "Modern desktop (GNOME)",
    blurb: "Like a Mac or laptop interface but adapted to touch. Lots of polish, more RAM-hungry.",
  },
  {
    id: "weston",
    illus: "weston",
    advTitle: "Weston",
    advSub: "reference wayland",
    title: "Bare wayland (Weston)",
    blurb: "Just a reference window manager. Nothing pretty. Pick this only if you're testing wayland.",
  },
];

/* Per-desktop CSS wireframes. Kept tiny: a frame plus a few divs.
 * Style hooks live in app.css under .illus / .illus.<kind>. */
function Illus({ kind }) {
  if (kind === "phosh") {
    return (
      <div className="illus phosh">
        <div className="bar" />
        <div className="grid">
          <i /><i /><i /><i /><i /><i /><i /><i /><i />
        </div>
        <div className="dock" />
      </div>
    );
  }
  if (kind === "plasma") {
    return (
      <div className="illus plasma">
        <div className="bar" />
        <div className="win">
          <div className="hd" />
          <div className="bd" />
        </div>
        <div className="taskbar">
          <i /><i /><i />
        </div>
      </div>
    );
  }
  if (kind === "sxmo") {
    return (
      <div className="illus sxmo">
        <div className="status">11:42 · sxmo · 82%</div>
        <div className="tags">
          <span className="t on">1</span>
          <span className="t">2</span>
          <span className="t">3</span>
        </div>
        <div className="prompt">$ _</div>
      </div>
    );
  }
  if (kind === "gnome") {
    return (
      <div className="illus gnome">
        <div className="topbar"><i /><i className="r" /></div>
        <div className="win">
          <div className="hd" />
          <div className="bd" />
        </div>
        <div className="dock">
          <i /><i /><i /><i />
        </div>
      </div>
    );
  }
  if (kind === "weston") {
    return (
      <div className="illus weston">
        <div className="panel" />
        <div className="cursor" />
      </div>
    );
  }
  /* none */
  return (
    <div className="illus none">
      <div className="line">peacock login:</div>
      <div className="line"><span className="cur">_</span></div>
    </div>
  );
}

export default function DesktopStep({ desktop, setDesktop, dm, setDm, mode }) {
  const advanced = mode === "advanced";

  /* When the user picks a desktop in basic mode, also slot in the
   * smart DM default so the wizard's review screen still has a value. */
  const pick = (id) => {
    setDesktop(id);
    if (!advanced) {
      setDm(DM_DEFAULTS[id] || "sddm");
    } else {
      if (id === "none") setDm("none");
      else if (dm === "none") setDm("sddm");
    }
  };

  return (
    <React.Fragment>
      <Head
        c="STEP 03 / 06 · USERLAND"
        t={advanced ? "Desktop & login" : "How do you want PeacockOS to look?"}
        s={advanced
          ? "Choose a graphical environment and the display manager that greets you. Pick None for a headless console image."
          : "Pick the look that fits how you'll use the phone. You can change this later by re-flashing."}
      />
      <div className="mbody fade">
        {advanced ? (
          /* ---- ADVANCED: original terse 6-card row ---- */
          <div className="tiles" style={{ marginBottom: 22 }}>
            {DESKTOPS.map(d => (
              <div
                key={d.id}
                className={"tile" + (desktop === d.id ? " on" : "")}
                onClick={() => pick(d.id)}
              >
                <div className="check">✓</div>
                <div className="tn">{d.advTitle}</div>
                <div className="tm">{d.advSub}</div>
              </div>
            ))}
          </div>
        ) : (
          /* ---- BASIC: big illustration cards with plain-English blurbs ---- */
          <div className="dtiles">
            {DESKTOPS.map(d => (
              <div
                key={d.id}
                className={"dtile" + (desktop === d.id ? " on" : "")}
                onClick={() => pick(d.id)}
              >
                <div className="check">✓</div>
                <Illus kind={d.illus} />
                <div className="dtn">{d.title}</div>
                <div className="dtb">{d.blurb}</div>
              </div>
            ))}
          </div>
        )}

        {/* DM row is advanced-only. In basic mode we already picked
            a sensible default behind the scenes. */}
        {advanced && (
          <Field l="Display manager" sub={desktop === "none" ? "n/a · headless" : "greeter"}>
            <Seg
              v={dm}
              set={setDm}
              opts={DMS}
              dis={desktop === "none" ? DMS.filter(x => x !== "none") : []}
            />
          </Field>
        )}
      </div>
    </React.Fragment>
  );
}
