/* Home.jsx — poster launcher (stage content) */
import React from "react";
import { PK, Btn, useKeys, FULL } from "./shared.jsx";

export default function Home({ go, resume, peacock }) {
  useKeys(React.useMemo(() => ({ b: () => go("build"), i: () => go("install") }), [go]));
  const pkCls = peacock === "ink" ? "pkhero pkfill" : "pkhero pkgrad";
  return (
    <div className="home fade">
      <PK src={FULL} className={pkCls} style={peacock === "ink" ? { "--pkc": "#201F24" } : undefined} />
      <div className="kick">PEACOCKOS — BUILD &amp; INSTALL · v0.9</div>
      <div className="big">
        <div className="l1">Peacock</div>
        <div className="l2">Builder</div>
      </div>
      <div className="lede">Build a PeacockOS image for a device — or install it onto the disk in front of you.</div>
      <div className="acts">
        <Btn variant="primary" cap="B" sub="FOR A DEVICE" onClick={() => go("build")}>Build an image</Btn>
        <Btn variant="ghost" cap="I" sub="FROM LIVE" onClick={() => go("install")}>Install to this device</Btn>
      </div>
      <div className="recent">
        <span>RECENT</span>
        <span style={{ opacity: .4 }}>—</span>
        <a onClick={resume}>resume · samsung-jflte · 64%</a>
        <span style={{ opacity: .4 }}>·</span>
        <a onClick={() => go("build")}>xiaomi-daisy · arch</a>
      </div>
    </div>
  );
}
