/* main.jsx — ReactDOM mount.
 *
 * Mirrors peacock-builder's main.jsx; the only divergence is App.jsx,
 * which here mounts only the install flow (no BuildFlow / Home tile
 * grid). styles/app.css is symlinked across the two binaries so they
 * share one source of CSS. */
import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App.jsx";
import "../styles/app.css";

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
