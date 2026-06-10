/* PortsSync.jsx — first-time-setup clone screen.
 *
 * Shown by the build gate when peacock-ports isn't present yet. It calls
 * SyncPorts() (which clones the device catalog), streams `git clone`
 * output via the "ports:log" Wails event, and calls onReady() once the
 * "ports:done" event fires. On "ports:error" it surfaces the failure with
 * Retry / Back-to-home actions.
 *
 * This page exists so the user never lands on an empty device list while
 * the catalog is still downloading — the whole wizard needs peacock-ports. */
import React from "react";
import { PK, Btn, FULL } from "./shared.jsx";
import { SyncPorts } from "./api.js";

export default function PortsSync({ onReady, onHome }) {
  const [line, setLine] = React.useState("");
  const [error, setError] = React.useState("");
  const [attempt, setAttempt] = React.useState(0);

  React.useEffect(() => {
    let alive = true;
    const rt = typeof window !== "undefined" ? window.runtime : null;
    const offs = [];
    if (rt && typeof rt.EventsOn === "function") {
      offs.push(rt.EventsOn("ports:log", (chunk) => {
        if (!alive) return;
        // Keep just the last non-empty line — the clone is chatty.
        const last = String(chunk).split("\n").filter(Boolean).pop();
        if (last) setLine(last);
      }));
      offs.push(rt.EventsOn("ports:done", () => { if (alive) onReady(); }));
      offs.push(rt.EventsOn("ports:error", (msg) => { if (alive) setError(String(msg || "unknown error")); }));
    }
    setError("");
    setLine("");
    // Kick the clone. If it resolves present=true synchronously (already
    // there, or dev mode), advance immediately without waiting on events.
    SyncPorts().then((st) => {
      if (alive && st && st.present) onReady();
    }).catch((e) => { if (alive) setError(String(e)); });
    return () => {
      alive = false;
      offs.forEach((off) => { if (typeof off === "function") off(); });
    };
  }, [attempt, onReady]);

  return (
    <div className="psync">
      <div className="psync-aura" />
      <PK src={FULL} className="psync-pk pkgrad" />
      {error ? (
        <React.Fragment>
          <div className="psync-tag err">SETUP FAILED</div>
          <h1 className="psync-h1">Couldn't fetch the device catalog.</h1>
          <p className="psync-body">{error}</p>
          <p className="psync-hint">
            Check your internet connection, or set <code>PEACOCK_PORTS_DIR</code> to
            a local peacock-ports checkout and restart.
          </p>
          <div className="psync-acts">
            <Btn variant="grad" ar="↻" onClick={() => setAttempt((n) => n + 1)}>Try again</Btn>
            <Btn variant="ghost" onClick={onHome}>Back to home</Btn>
          </div>
        </React.Fragment>
      ) : (
        <React.Fragment>
          <div className="psync-tag">FIRST-TIME SETUP</div>
          <h1 className="psync-h1">Getting things ready…</h1>
          <p className="psync-body">
            Fetching the list of supported devices. This only happens once —
            it'll be quick.
          </p>
          <div className="psync-track"><i /></div>
          <div className="psync-log">{line || "Connecting…"}</div>
        </React.Fragment>
      )}
    </div>
  );
}
