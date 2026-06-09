package main

// RunDoctor binding — wraps internal/host's table-driven probe runner
// so the GUI's "Check host" tile can show real results instead of
// stub data. Read-only (no shellouts beyond what the probe table does
// itself), safe to call from any goroutine, no Wails context needed.
//
// The JSON wire shapes (DoctorReport / DoctorSummary / ProbeResultDTO)
// are shared with peacock-installer via internal/guidto.

import (
	"peacock/internal/guidto"
	"peacock/internal/host"
)

// RunDoctor runs the host probe table and returns a JSON-friendly
// summary + per-probe results. Args mirror the cobra `peacock doctor`
// flags:
//   - flavor: "" (run all flavor probes) or "arch"/"debian"/"alpine"
//   - device: "" (skip device-family probes) or a family name
//     ("android", "fastboot", "oppo-a16", ...) — passed to ProbeOpts as
//     DeviceFamily; the probe table's device entries match against it.
//   - useHostChroot: when true, the call sets ProbeOpts.UseHostChroot
//     to the flavor (or "arch" if flavor is empty), which collapses
//     flavor-bootstrap probes to Skipped and appends host-chroot
//     probes.
func (a *App) RunDoctor(flavor, device string, useHostChroot bool) (guidto.DoctorReport, error) {
	opts := host.ProbeOpts{
		Flavor:       flavor,
		DeviceFamily: device,
	}
	if useHostChroot {
		hc := flavor
		if hc == "" {
			hc = "arch"
		}
		opts.UseHostChroot = hc
	}

	results := host.FilterAndRunWithHostChroot(opts)
	summary := host.SummarizeResults(results)

	dto := make([]guidto.ProbeResultDTO, 0, len(results))
	for _, r := range results {
		dto = append(dto, guidto.ProbeResultDTO{
			Group:       string(r.Group),
			Name:        r.Name,
			Path:        r.Path,
			Version:     r.Version,
			Status:      string(r.Status),
			InstallHint: r.InstallHint,
			Why:         r.Why,
		})
	}

	return guidto.DoctorReport{
		Summary: guidto.DoctorSummary{
			OK:      summary.OK,
			Missing: summary.Missing,
			Broken:  summary.Broken,
			Skipped: summary.Skipped,
		},
		Results: dto,
	}, nil
}
