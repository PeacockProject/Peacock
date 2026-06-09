package main

// RunDoctor binding — wraps internal/host's table-driven probe runner
// so the GUI's "Check host" tile can show real results instead of
// stub data. Read-only (no shellouts beyond what the probe table does
// itself), safe to call from any goroutine, no Wails context needed.

import (
	"peacock/internal/host"
)

// DoctorReport is the JSON-shaped result the React side consumes. We
// re-export host.Result fields as a DTO so the JSON output stays stable
// even if internal/host renames or reshuffles its struct tags.
type DoctorReport struct {
	Summary DoctorSummary     `json:"summary"`
	Results []ProbeResultDTO  `json:"results"`
}

// DoctorSummary mirrors host.Summary in a JSON form the frontend can
// destructure directly (`{ok, missing, broken}`).
type DoctorSummary struct {
	OK      int `json:"ok"`
	Missing int `json:"missing"`
	Broken  int `json:"broken"`
	Skipped int `json:"skipped"`
}

// ProbeResultDTO is the per-probe JSON shape. Fields match the React
// mock's expectations: group/name surface in section headers; status
// drives the icon; install_hint is shown on hover/expand; why is the
// secondary description; path + version round it out.
type ProbeResultDTO struct {
	Group       string `json:"group"`
	Name        string `json:"name"`
	Path        string `json:"path,omitempty"`
	Version     string `json:"version,omitempty"`
	Status      string `json:"status"`
	InstallHint string `json:"install_hint,omitempty"`
	Why         string `json:"why,omitempty"`
}

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
func (a *App) RunDoctor(flavor, device string, useHostChroot bool) (DoctorReport, error) {
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

	dto := make([]ProbeResultDTO, 0, len(results))
	for _, r := range results {
		dto = append(dto, ProbeResultDTO{
			Group:       string(r.Group),
			Name:        r.Name,
			Path:        r.Path,
			Version:     r.Version,
			Status:      string(r.Status),
			InstallHint: r.InstallHint,
			Why:         r.Why,
		})
	}

	return DoctorReport{
		Summary: DoctorSummary{
			OK:      summary.OK,
			Missing: summary.Missing,
			Broken:  summary.Broken,
			Skipped: summary.Skipped,
		},
		Results: dto,
	}, nil
}
