package main

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
	"strings"

	"peacock/internal/host"

	"github.com/spf13/cobra"
)

var (
	doctorFlavor        string
	doctorDevice        string
	doctorJSON          bool
	doctorUseHostChroot string
)

var doctorCmd = &cobra.Command{
	Use:   "doctor",
	Short: "Audit the host for Peacock build prerequisites",
	Long: `peacock doctor inspects the host for the tools and conditions required to
run a Peacock build. It is read-only — no installs, no chroot work — and exits
non-zero when any required tool is missing or broken so CI can gate on it.

Examples:
  peacock doctor                          # generic audit (everything every flavor needs)
  peacock doctor --flavor arch            # only the arch flavor's bootstrap prereqs
  peacock doctor --device oppo-a16        # adds device-family prereqs
  peacock doctor --json                   # machine-readable output

When --use-host-chroot <flavor> is set, the per-flavor probes get collapsed
to the chroot tool itself (chroot/tar/curl); everything else lives inside
the host chroot and gets installed there at build time.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		opts := host.ProbeOpts{
			Flavor:        doctorFlavor,
			DeviceFamily:  doctorDevice,
			UseHostChroot: doctorUseHostChroot,
		}

		results := host.FilterAndRunWithHostChroot(opts)

		if doctorJSON {
			return printJSON(results)
		}
		summary := printHuman(opts, results)
		if summary.IsFatal() {
			// Non-zero exit code without printing the cobra usage.
			os.Exit(1)
		}
		return nil
	},
}

func printJSON(results []host.Result) error {
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	if err := enc.Encode(results); err != nil {
		return fmt.Errorf("doctor: json encode: %w", err)
	}
	summary := host.SummarizeResults(results)
	if summary.IsFatal() {
		os.Exit(1)
	}
	return nil
}

func printHuman(opts host.ProbeOpts, results []host.Result) host.Summary {
	hdr := "peacock doctor"
	parts := []string{}
	if opts.Flavor != "" {
		parts = append(parts, "flavor="+opts.Flavor)
	}
	if opts.DeviceFamily != "" {
		parts = append(parts, "device="+opts.DeviceFamily)
	}
	if opts.UseHostChroot != "" {
		parts = append(parts, "host-chroot="+opts.UseHostChroot)
	}
	if len(parts) > 0 {
		hdr = hdr + " — " + strings.Join(parts, " ")
	}
	fmt.Println(hdr)
	fmt.Println()

	// Bucket by group, preserve canonical group ordering.
	byGroup := map[host.Group][]host.Result{}
	groupOrder := []host.Group{}
	for _, r := range results {
		if _, ok := byGroup[r.Group]; !ok {
			groupOrder = append(groupOrder, r.Group)
		}
		byGroup[r.Group] = append(byGroup[r.Group], r)
	}

	for _, g := range groupOrder {
		fmt.Printf("[%s]\n", g)
		for _, r := range byGroup[g] {
			printResultLine(r)
		}
		fmt.Println()
	}

	summary := host.SummarizeResults(results)
	fmt.Printf("Summary: %d ok, %d missing, %d broken", summary.OK, summary.Missing, summary.Broken)
	if summary.Skipped > 0 {
		fmt.Printf(", %d skipped (--use-host-chroot collapses these)", summary.Skipped)
	}
	fmt.Println(".")
	if !doctorJSON {
		fmt.Println("Run `peacock doctor --json` for machine output.")
	}
	return summary
}

func printResultLine(r host.Result) {
	const nameCol = 32
	label := r.Name
	if len(label) > nameCol-2 {
		label = label[:nameCol-2]
	}
	dots := nameCol - len(label)
	if dots < 1 {
		dots = 1
	}
	pad := strings.Repeat(".", dots)

	switch r.Status {
	case host.StatusOK:
		ver := ""
		if r.Version != "" {
			ver = " (" + r.Version + ")"
		}
		path := r.Path
		if path == "" {
			path = "ok"
		}
		fmt.Printf("  %s %s %s%s [ok]\n", label, pad, path, ver)
	case host.StatusMissing:
		fmt.Printf("  %s %s MISSING\n", label, pad)
		if r.InstallHint != "" {
			fmt.Printf("    install: %s\n", r.InstallHint)
		}
		if r.Why != "" {
			fmt.Printf("    why:     %s\n", r.Why)
		}
	case host.StatusBroken:
		ver := ""
		if r.Version != "" {
			ver = " (" + r.Version + ")"
		}
		fmt.Printf("  %s %s BROKEN%s\n", label, pad, ver)
		if r.InstallHint != "" {
			fmt.Printf("    install: %s\n", r.InstallHint)
		}
		if r.Why != "" {
			fmt.Printf("    why:     %s\n", r.Why)
		}
	case host.StatusSkipped:
		fmt.Printf("  %s %s skipped\n", label, pad)
		if r.Why != "" {
			fmt.Printf("    why:     %s\n", r.Why)
		}
	}
}

func init() {
	rootCmd.AddCommand(doctorCmd)
	doctorCmd.Flags().StringVar(&doctorFlavor, "flavor", "", "Only audit one flavor's prereqs (arch|debian|alpine)")
	doctorCmd.Flags().StringVar(&doctorDevice, "device", "", "Add device-family prereqs (e.g. oppo-a16, samsung-jflte)")
	doctorCmd.Flags().BoolVar(&doctorJSON, "json", false, "Emit machine-readable JSON")
	doctorCmd.Flags().StringVar(&doctorUseHostChroot, "use-host-chroot", "", "Audit assuming --use-host-chroot=<flavor> will be used at build time")
}

// silence unused import warnings if cobra/sort ever drift; keep for
// future "sort results by group then name" toggles.
var _ = sort.Strings
