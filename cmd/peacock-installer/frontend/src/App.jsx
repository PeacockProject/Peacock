/* App.jsx — top-level for peacock-installer.
 *
 * Where peacock-builder's App.jsx is a small router across Home /
 * BuildFlow / InstallFlow, the installer is single-purpose: it boots
 * straight into the install wizard. There is no Home screen, no
 * device-build flow; the only state we keep is "the wizard is
 * running" — and even that's just for symmetry with how the wizard
 * itself signals completion (it calls onHome, which here exits the
 * window or returns to a "thanks, please reboot" splash).
 *
 * The iridescent accent + serif font seeding is duplicated from the
 * builder so the two binaries look pixel-identical at startup. */
import React from "react";
import InstallFlow from "./InstallFlow.jsx";

const ACCENTS = {
  iridescent: {
    stops: ["#23B7AE", "#2E86C8", "#6C63D8"],
    blue: "#2E86C8",
    css: "linear-gradient(120deg,#23B7AE,#2E86C8 50%,#6C63D8)",
  },
};

function applyAccent() {
  const a = ACCENTS.iridescent;
  const root = document.documentElement.style;
  root.setProperty("--irid", a.css);
  root.setProperty("--blue", a.blue);
  const stops = document.querySelectorAll("#irid stop");
  if (stops.length === 3) a.stops.forEach((c, i) => stops[i].setAttribute("stop-color", c));
  root.setProperty("--serif", `'Instrument Serif',serif`);
}

applyAccent();

export default function App() {
  // onHome on the install flow signals "user chose to leave" — for an
  // installer the only sensible thing is to reset to the welcome step
  // so an operator can re-run the wizard against another disk without
  // restarting the binary. We rotate a key to remount InstallFlow.
  const [iteration, setIteration] = React.useState(0);
  const onHome = React.useCallback(() => setIteration(n => n + 1), []);
  return (
    <div className="viewwrap">
      <InstallFlow key={"i-" + iteration} onHome={onHome} appClass="" />
    </div>
  );
}
