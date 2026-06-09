import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Wails-compatible defaults shared with peacock-builder's vite.config.js.
// Relative base so the embedded dist/ resolves asset URLs without an
// absolute mount path; assetsDir lands JS/CSS bundles where go:embed
// picks them up. preserveSymlinks is OFF (default) so the symlinked
// shared files in src/ resolve to the builder's tree at build time —
// Vite traces through them and bundles their contents.
export default defineConfig({
  plugins: [react()],
  base: "./",
  build: {
    outDir: "dist",
    assetsDir: "assets",
    emptyOutDir: true,
  },
  server: {
    port: 5174,
    strictPort: false,
  },
});
