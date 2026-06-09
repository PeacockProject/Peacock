/* devMock.js — simulated build / install progress for `npm run dev`.
 *
 * Phase 1's Run.jsx shipped these directly so the wizard rendered
 * progress-bar movement without a Wails backend. The maintainer kept
 * the dev fallback because the wizard is deployed to a Cloudflare
 * Workers preview where friends try it out — pulling the simulated
 * path entirely would break that preview.
 *
 * Phase 4 (in-process pipeline call) split them out: Run.jsx
 * subscribes to real Wails events when window.go is present, and
 * imports these mocks lazily when window.go is null (the preview).
 *
 * Keeping them in their own module lets Vite tree-shake them out of
 * the Wails-only production bundle when the maintainer switches a
 * build flag — today both bundles include them. */

import React from "react";

const L = (t, prog, node) => ({ t, prog, node });

export const BUILD_PHASES = [
  { at: 0, label: "Resolving deps" }, { at: 18, label: "Building kernel" },
  { at: 34, label: "Building busybox" }, { at: 48, label: "Initramfs" },
  { at: 60, label: "Rootfs" }, { at: 80, label: "Configuring" }, { at: 92, label: "Disk image" },
];

export function buildScript(dev, desktop) {
  return [
    L("12:04:18", 6, <span>Resolving dependencies…</span>),
    L("12:04:20", 12, <span>→ <span className="b">6</span> local ports · <span className="b">142</span> remote packages</span>),
    L("12:04:26", 20, <span>building <span className="b">linux-{dev.code}</span> 3.4.0…</span>),
    L("12:05:02", 30, <span><span className="g">✓</span> zImage <span className="y">(4.2 MB)</span></span>),
    L("12:05:03", 34, <span><span className="g">✓</span> modules.tar.gz</span>),
    L("12:05:10", 40, <span>building <span className="b">busybox</span> 1.36.1…</span>),
    L("12:05:24", 46, <span><span className="g">✓</span> busybox</span>),
    L("12:05:25", 50, <span>peacock-mkinitfs build --device {dev.code}</span>),
    L("12:05:40", 56, <span><span className="g">✓</span> initramfs.cpio.gz</span>),
    L("12:05:42", 60, <span>setting up image build chroot…</span>),
    L("12:06:01", 66, <span>installing packages to rootfs…</span>),
    L("12:06:03", 72, <span>&nbsp;&nbsp;→ {desktop === "none" ? "base" : desktop} mesa <span className="b">+142</span></span>),
    L("12:06:30", 80, <span>enabling services · staging extlinux…</span>),
    L("12:06:45", 88, <span>creating disk image (1920 MB)…</span>),
    L("12:06:58", 96, <span>mkfs.ext4 · <span className="b">ROOT</span></span>),
    L("12:07:05", 100, <span><span className="g">✓</span> build complete</span>),
  ];
}

export const INSTALL_PHASES = [
  { at: 0, label: "Partitioning" }, { at: 20, label: "Formatting" },
  { at: 34, label: "Copying system" }, { at: 78, label: "Bootloader" }, { at: 92, label: "Finishing" },
];

export function installScript(disk, user) {
  return [
    L("·", 5, <span>Creating partition table on <span className="b">{disk.node}</span>…</span>),
    L("·", 12, <span><span className="g">✓</span> {disk.node}1 boot · {disk.node}2 root</span>),
    L("·", 20, <span>mkfs.ext4 -L ROOT {disk.node}2…</span>),
    L("·", 30, <span><span className="g">✓</span> filesystems ready</span>),
    L("·", 36, <span>Copying PeacockOS to target…</span>),
    L("·", 52, <span>&nbsp;&nbsp;unpacking rootfs · 41,206 files</span>),
    L("·", 68, <span>&nbsp;&nbsp;<span className="y">rsync</span> /usr /etc /var · 1.7 GB</span>),
    L("·", 76, <span><span className="g">✓</span> system copied</span>),
    L("·", 80, <span>Creating user <span className="b">{user || "peacock"}</span> · setting hostname</span>),
    L("·", 86, <span>Installing bootloader (extlinux)…</span>),
    L("·", 92, <span>Generating initramfs · fstab…</span>),
    L("·", 100, <span><span className="g">✓</span> installation complete</span>),
  ];
}

// hasWails returns true when the Wails runtime has injected its
// bindings on the page. The pure-vite dev preview leaves window.go
// undefined; the production build (`wails build`) populates it. We
// use this in Run.jsx to decide whether to subscribe to real events
// or fall back to the simulated scripts above.
export function hasWails() {
  return typeof window !== "undefined" && !!window.go && !!window.go.main;
}
