/* meta.js — app-wide constants shared by both frontends.
 *
 * APP_VERSION is the single source of truth for the version string shown
 * in the UI (Home kicker, status bars, the installer rail). Bump it here
 * and every surface follows. The installer frontend reaches this file
 * through its symlinked sources (Vite resolves relative imports against
 * the builder tree). */
export const APP_VERSION = "0.9";
