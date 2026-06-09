package main

// ListDevices binding — surfaces the real peacock-ports/device/* catalog
// to the React device-picker in BuildFlow.jsx. We only return entries
// whose dir contains a device.toml (i.e. the "this is a phone you can
// flash to" ports), not the auxiliary ports like linux-<dev>, lk2nd-<dev>,
// firmware-<dev>, samsung-jflte-display-fix, etc. that also live under
// device/ but aren't user-selectable targets.

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"peacock/internal/manifest"
)

// DeviceMeta is the JSON-friendly shape the frontend consumes. Field
// names match the existing mock data in BuildFlow.jsx so the device
// tile renderer doesn't need to change. ID + Code are kept distinct
// (the React mock uses both) — for real ports they're the same string.
//
// SoC + Status come from device.toml's [device] block now; Support
// comes from device.toml's [support] table. The frontend's
// DEFAULT_DEVICE_SUPPORT stub is only used when running in dev-mode
// (no Wails runtime), e.g. the Cloudflare preview deploy.
type DeviceMeta struct {
	ID      string            `json:"id"`
	Name    string            `json:"name"`
	Code    string            `json:"code"`
	SoC     string            `json:"soc"`
	Arch    string            `json:"arch"`
	Status  string            `json:"status"`
	Tag     string            `json:"tag"` // legacy alias of Status for the existing React mock
	Support map[string]string `json:"support,omitempty"`
}

// portsRoot finds the peacock-ports tree. Resolution order:
//  1. $PEACOCK_PORTS_DIR
//  2. ./peacock-ports relative to cwd (matches build_setup.go)
//  3. ../peacock-ports relative to the binary's dir (handy when the
//     GUI is launched outside the Peacock repo, but the maintainer's
//     dev layout is Peacock + peacock-ports as siblings)
func portsRoot() (string, error) {
	if v := os.Getenv("PEACOCK_PORTS_DIR"); v != "" {
		if _, err := os.Stat(v); err == nil {
			return v, nil
		}
	}
	if _, err := os.Stat("peacock-ports"); err == nil {
		return "peacock-ports", nil
	}
	if exe, err := os.Executable(); err == nil {
		candidate := filepath.Join(filepath.Dir(exe), "..", "peacock-ports")
		if _, err := os.Stat(candidate); err == nil {
			return candidate, nil
		}
		// Also try a sibling of the Peacock repo (../../peacock-ports
		// from a binary in cmd/peacock-builder/build/bin/).
		candidate = filepath.Join(filepath.Dir(exe), "..", "..", "..", "..", "peacock-ports")
		if _, err := os.Stat(candidate); err == nil {
			return candidate, nil
		}
	}
	return "", fmt.Errorf("peacock-ports not found; set PEACOCK_PORTS_DIR or launch from a Peacock checkout")
}

// ListDevices returns the user-selectable device catalog. Only ports
// under device/ that carry both a device.toml AND a package.toml are
// considered real devices; siblings like linux-<dev>, lk2nd-<dev>, and
// firmware-<dev> are dropped because they describe sub-ports, not
// flashable targets.
//
// The frontend caches the result; ListDevices is cheap (a handful of
// readDirs + TOML parses) so we don't memoize on the Go side.
func (a *App) ListDevices() ([]DeviceMeta, error) {
	root, err := portsRoot()
	if err != nil {
		return nil, err
	}
	deviceDir := filepath.Join(root, "device")

	entries, err := os.ReadDir(deviceDir)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", deviceDir, err)
	}

	out := make([]DeviceMeta, 0, len(entries))
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		dir := filepath.Join(deviceDir, e.Name())
		devTomlPath := filepath.Join(dir, "device.toml")
		pkgTomlPath := filepath.Join(dir, "package.toml")
		if _, err := os.Stat(devTomlPath); err != nil {
			continue
		}
		if _, err := os.Stat(pkgTomlPath); err != nil {
			continue
		}
		dev, err := manifest.LoadDevice(devTomlPath)
		if err != nil {
			// Skip malformed manifests; the GUI will surface them
			// via doctor later if needed.
			continue
		}
		_, _ = manifest.LoadPackage(pkgTomlPath) // reserved; not consumed today

		name := dev.Device.Name
		if name == "" {
			name = e.Name()
		}
		status := dev.Device.Status
		if status == "" {
			status = "experimental" // port exists but maturity unset → safest default
		}

		out = append(out, DeviceMeta{
			ID:      e.Name(),
			Name:    name,
			Code:    e.Name(),
			SoC:     dev.Device.SoC,
			Arch:    dev.Device.Architecture,
			Status:  status,
			Tag:     status, // legacy field; same value for back-compat
			Support: dev.Support,
		})
	}

	sort.Slice(out, func(i, j int) bool { return out[i].Code < out[j].Code })
	return out, nil
}

