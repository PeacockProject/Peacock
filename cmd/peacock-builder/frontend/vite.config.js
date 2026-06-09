import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Wails-compatible defaults: relative base so the embedded dist/ resolves
// asset URLs without needing an absolute mount path; assetsDir lands the
// JS/CSS bundles where go:embed picks them up.
export default defineConfig({
  plugins: [react()],
  base: "./",
  build: {
    outDir: "dist",
    assetsDir: "assets",
    emptyOutDir: true,
  },
  server: {
    port: 5173,
    strictPort: false,
  },
});
