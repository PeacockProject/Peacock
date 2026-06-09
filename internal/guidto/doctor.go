// Package guidto holds the JSON-shaped DTO types shared by the Wails
// GUI binaries (peacock-builder, peacock-installer). The per-binary
// RunDoctor logic stays in each binary — only the wire shapes the React
// side consumes live here, so the two doctor screens render identically.
package guidto

// DoctorReport is the JSON-shaped result the React side consumes. We
// re-export probe result fields as a DTO so the JSON output stays
// stable even if internal/host renames or reshuffles its struct tags.
type DoctorReport struct {
	Summary DoctorSummary    `json:"summary"`
	Results []ProbeResultDTO `json:"results"`
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
// frontend's expectations: group/name surface in section headers;
// status drives the icon; install_hint is shown on hover/expand; why is
// the secondary description; path + version round it out.
type ProbeResultDTO struct {
	Group       string `json:"group"`
	Name        string `json:"name"`
	Path        string `json:"path,omitempty"`
	Version     string `json:"version,omitempty"`
	Status      string `json:"status"`
	InstallHint string `json:"install_hint,omitempty"`
	Why         string `json:"why,omitempty"`
}
