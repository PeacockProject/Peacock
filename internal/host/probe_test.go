package host

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestVersionAtLeast(t *testing.T) {
	cases := []struct {
		have, want string
		ok         bool
	}{
		{"1.25.5", "1.25.5", true},
		{"1.25.6", "1.25.5", true},
		{"1.26.0", "1.25.5", true},
		{"2.0", "1.25.5", true},
		{"1.25.4", "1.25.5", false},
		{"1.24.99", "1.25.5", false},
		{"3.12.5", "3.11", true},
		{"3.10.0", "3.11", false},
		{"3.11", "3.11", true},
	}
	for _, c := range cases {
		got := versionAtLeast(c.have, c.want)
		if got != c.ok {
			t.Errorf("versionAtLeast(%q, %q) = %v, want %v", c.have, c.want, got, c.ok)
		}
	}
}

func TestTableNotEmpty(t *testing.T) {
	tbl := Table()
	if len(tbl) == 0 {
		t.Fatal("probe table must not be empty")
	}
	seen := make(map[string]bool)
	for _, p := range tbl {
		if p.Name == "" {
			t.Errorf("probe %+v has empty Name", p)
		}
		if p.Group == "" {
			t.Errorf("probe %q has empty Group", p.Name)
		}
		if p.run == nil {
			t.Errorf("probe %q has nil runner", p.Name)
		}
		if seen[p.Name] {
			t.Errorf("duplicate probe name %q", p.Name)
		}
		seen[p.Name] = true
	}
}

func TestFilterByFlavor(t *testing.T) {
	all := FilterAndRun(ProbeOpts{})
	debOnly := FilterAndRun(ProbeOpts{Flavor: "debian"})
	if len(debOnly) >= len(all) {
		t.Errorf("expected debian-only to have fewer probes than union, got %d vs %d", len(debOnly), len(all))
	}
	for _, r := range debOnly {
		if r.Name == "apk" {
			t.Errorf("apk probe should be filtered out when flavor=debian")
		}
	}
}

func TestFilterByDeviceFamily(t *testing.T) {
	noDev := FilterAndRun(ProbeOpts{})
	for _, r := range noDev {
		if r.Group == GroupDevice {
			t.Errorf("device probes should not appear when DeviceFamily is empty, got %q", r.Name)
		}
	}
	withDev := FilterAndRun(ProbeOpts{DeviceFamily: "oppo-a16"})
	sawDevice := false
	for _, r := range withDev {
		if r.Group == GroupDevice {
			sawDevice = true
			break
		}
	}
	if !sawDevice {
		t.Errorf("expected device probes when DeviceFamily=oppo-a16")
	}
}

func TestFileExistsRunner(t *testing.T) {
	tmp := t.TempDir()
	missing := filepath.Join(tmp, "nope")
	present := filepath.Join(tmp, "yes")
	if err := os.WriteFile(present, []byte("x"), 0644); err != nil {
		t.Fatal(err)
	}

	p := Probe{run: fileExistsRunner(missing)}
	r := p.Run(ProbeOpts{})
	if r.Status != StatusMissing {
		t.Errorf("missing path = %v, want missing", r.Status)
	}

	p = Probe{run: fileExistsRunner(present)}
	r = p.Run(ProbeOpts{})
	if r.Status != StatusOK {
		t.Errorf("present path = %v, want ok", r.Status)
	}
}

func TestSummarize(t *testing.T) {
	results := []Result{
		{Status: StatusOK},
		{Status: StatusMissing},
		{Status: StatusMissing},
		{Status: StatusBroken},
		{Status: StatusSkipped},
	}
	s := SummarizeResults(results)
	if s.OK != 1 || s.Missing != 2 || s.Broken != 1 || s.Skipped != 1 {
		t.Errorf("unexpected summary: %+v", s)
	}
	if !s.IsFatal() {
		t.Errorf("summary with missing+broken should be fatal")
	}

	clean := SummarizeResults([]Result{{Status: StatusOK}})
	if clean.IsFatal() {
		t.Errorf("clean summary should not be fatal")
	}
}

func TestHostChrootRoot(t *testing.T) {
	root, err := HostChrootRoot("alpine")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.HasSuffix(root, "host-chroots/alpine") {
		t.Errorf("unexpected host-chroot path: %s", root)
	}

	if _, err := HostChrootRoot("freebsd"); err == nil {
		t.Errorf("expected error for unsupported flavor")
	}
}

func TestEnsureHostChrootRejectsUnsupportedFlavor(t *testing.T) {
	// The real bootstrap path needs network + root, so it is
	// integration-only. What we CAN assert hermetically is that an
	// unsupported flavor is rejected up front (via HostChrootRoot)
	// without any download/extract side effects, and that the
	// now-implemented entrypoint no longer surfaces the old
	// "not yet implemented" sentinel.
	if _, err := EnsureHostChroot("freebsd"); err == nil {
		t.Errorf("expected error for unsupported flavor")
	} else if strings.Contains(err.Error(), "not yet implemented") {
		t.Errorf("EnsureHostChroot still returns the stale not-implemented error: %v", err)
	}
}

func TestHostChrootCollapsesProbes(t *testing.T) {
	opts := ProbeOpts{UseHostChroot: "arch"}
	results := FilterAndRunWithHostChroot(opts)

	sawCollapsed := false
	sawHostChrootGroup := false
	for _, r := range results {
		if r.Group == GroupHostChroot {
			sawHostChrootGroup = true
		}
		if r.Name == "qemu-aarch64-static" && r.Status == StatusSkipped {
			sawCollapsed = true
		}
	}
	if !sawCollapsed {
		t.Errorf("expected qemu-aarch64-static to be skipped under --use-host-chroot=arch")
	}
	if !sawHostChrootGroup {
		t.Errorf("expected host-chroot group probes to appear under --use-host-chroot=arch")
	}
}

func TestTarballURL(t *testing.T) {
	if u := TarballURL("arch"); u == "" {
		t.Errorf("arch tarball URL missing")
	}
	if u := TarballURL("nope"); u != "" {
		t.Errorf("unknown flavor should return empty URL, got %q", u)
	}
}
