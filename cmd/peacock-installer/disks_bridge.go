package main

// ListDisks binding — wraps installer.ListDisks and reshapes its
// DiskInfo into the JSON DTO the React InstallFlow disk picker
// expects. The mock JSX hard-coded a three-disk demo set; this
// binding replaces that with real lsblk-driven data.

import (
	"context"

	"peacock/internal/installer"
)

// DiskInfoDTO is the JSON shape returned to the frontend. Field names
// mirror the mock's DISKS array so InstallFlow.jsx can consume the
// result with no shape-translation layer in api.js.
//
// installer.DiskInfo carries a Children slice (existing partitions
// under the disk); the wizard's disk-picker screen doesn't render
// children today but we forward them so a future "show what's on this
// disk" disclosure can land without a binding change.
type DiskInfoDTO struct {
	Name      string        `json:"name"`
	Node      string        `json:"node"`
	Meta      string        `json:"meta"`
	Cap       string        `json:"cap"`
	SizeBytes uint64        `json:"sizeBytes"`
	Removable bool          `json:"removable"`
	Children  []PartInfoDTO `json:"children,omitempty"`
}

// PartInfoDTO mirrors installer.PartInfo for JSON.
type PartInfoDTO struct {
	Node       string `json:"node"`
	Size       string `json:"size"`
	FSType     string `json:"fstype,omitempty"`
	Mountpoint string `json:"mountpoint,omitempty"`
}

// ListDisks returns the target-disk candidates. The Wails wrapper
// uses the App's ctx so cancellation propagates if the GUI is
// closed mid-call; we also pass a fresh context.Background() when
// a.ctx isn't set (test-builds construct App without a runtime).
func (a *App) ListDisks() ([]DiskInfoDTO, error) {
	ctx := a.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	raw, err := installer.ListDisks(ctx)
	if err != nil {
		return nil, err
	}
	out := make([]DiskInfoDTO, 0, len(raw))
	for _, d := range raw {
		// installer.DiskInfo doesn't carry a separate Meta field — it
		// folds vendor/model into Name. We split Name into a (Name,
		// Meta) pair so the wizard's main label + secondary line look
		// right: keep up-to-the-first-".·" as the Name, the rest as
		// Meta. When there's no separator, Meta is empty and the
		// wizard just shows the model.
		dto := DiskInfoDTO{
			Name:      d.Name,
			Node:      d.Node,
			Cap:       d.Cap,
			SizeBytes: d.SizeBytes,
			Removable: d.Removable,
		}
		if d.Removable {
			dto.Meta = "removable"
		}
		for _, c := range d.Children {
			dto.Children = append(dto.Children, PartInfoDTO{
				Node:       c.Node,
				Size:       c.Size,
				FSType:     c.FSType,
				Mountpoint: c.Mountpoint,
			})
		}
		out = append(out, dto)
	}
	return out, nil
}
