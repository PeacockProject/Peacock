package host

import "testing"

// archListingFixture is a realistic snippet of a geo-mirror
// iso/latest/ directory listing. The happy path no longer scrapes this,
// but parseArchBootstrapListing is retained + tested as a pure fallback.
const archListingFixture = `<!DOCTYPE html>
<html>
 <head><title>Index of /iso/latest/</title></head>
 <body>
<h1>Index of /iso/latest/</h1>
<table>
<tr><td><a href="../">../</a></td></tr>
<tr><td><a href="archlinux-bootstrap-x86_64.tar.zst">archlinux-bootstrap-x86_64.tar.zst</a></td></tr>
<tr><td><a href="archlinux-bootstrap-2024.06.01-x86_64.tar.zst">archlinux-bootstrap-2024.06.01-x86_64.tar.zst</a></td></tr>
<tr><td><a href="archlinux-bootstrap-2024.06.01-x86_64.tar.zst.sig">archlinux-bootstrap-2024.06.01-x86_64.tar.zst.sig</a></td></tr>
<tr><td><a href="sha256sums.txt">sha256sums.txt</a></td></tr>
</table>
</body>
</html>`

func TestParseArchBootstrapListing(t *testing.T) {
	tests := []struct {
		name    string
		html    string
		want    string
		wantErr bool
	}{
		{
			name: "realistic listing",
			html: archListingFixture,
			want: "archlinux-bootstrap-2024.06.01-x86_64.tar.zst",
		},
		{
			name: "picks newest of several dates",
			html: `archlinux-bootstrap-2023.01.01-x86_64.tar.zst
archlinux-bootstrap-2024.12.01-x86_64.tar.zst
archlinux-bootstrap-2024.06.01-x86_64.tar.zst`,
			want: "archlinux-bootstrap-2024.12.01-x86_64.tar.zst",
		},
		{
			name:    "no match",
			html:    `<html><body>nothing relevant here</body></html>`,
			wantErr: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := parseArchBootstrapListing(tt.html)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got %q", got)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got != tt.want {
				t.Fatalf("got %q, want %q", got, tt.want)
			}
		})
	}
}

func TestExpectedHashFor(t *testing.T) {
	// Arch/Alpine form: "<hash>  <file>" (two-space separator).
	archSums := `abc123def456  archlinux-bootstrap-2024.06.01-x86_64.tar.zst
0000000000000000  archlinux-2024.06.01-x86_64.iso`
	// Arch's real sha256sums.txt lists BOTH the stable and the dated
	// filename, with the SAME hash. We must resolve the stable name by
	// basename out of a multi-line manifest.
	archStableAndDatedSums := `9f9f9f9f9f9f  archlinux-bootstrap-x86_64.tar.zst
9f9f9f9f9f9f  archlinux-bootstrap-2026.06.01-x86_64.tar.zst`
	// Debian (user-supplied rootfs) sidecar form: same shape, sometimes
	// with a "./" prefix or a binary-mode "*" marker.
	debianSums := `deadbeefcafe *./debian-rootfs.tar.xz
1111111111111111  debian-other.tar.xz`
	// Alpine miniroot sidecar form.
	alpineSums := `feedface9999  alpine-minirootfs-3.20.0-x86_64.tar.gz`

	tests := []struct {
		name     string
		content  string
		filename string
		want     string
		wantErr  bool
	}{
		{"arch double-space", archSums, "archlinux-bootstrap-2024.06.01-x86_64.tar.zst", "abc123def456", false},
		{"arch by basename", archSums, "https://x/archlinux-bootstrap-2024.06.01-x86_64.tar.zst", "abc123def456", false},
		{"arch stable name from multi-line manifest", archStableAndDatedSums, "archlinux-bootstrap-x86_64.tar.zst", "9f9f9f9f9f9f", false},
		{"arch dated name from same manifest", archStableAndDatedSums, "archlinux-bootstrap-2026.06.01-x86_64.tar.zst", "9f9f9f9f9f9f", false},
		{"debian star and dotslash", debianSums, "debian-rootfs.tar.xz", "deadbeefcafe", false},
		{"alpine", alpineSums, "alpine-minirootfs-3.20.0-x86_64.tar.gz", "feedface9999", false},
		{"missing entry fails closed", archSums, "not-present.tar.gz", "", true},
		{"empty manifest fails closed", "", "anything.tar.gz", "", true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := expectedHashFor(tt.content, tt.filename)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got %q", got)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got != tt.want {
				t.Fatalf("got %q, want %q", got, tt.want)
			}
		})
	}
}

func TestStripComponentsFor(t *testing.T) {
	tests := map[string]int{
		"arch":    1, // bootstrap nests under root.x86_64/
		"debian":  0, // user-supplied rootfs expected flat
		"alpine":  0, // miniroot extracts flat
		"unknown": 0,
	}
	for flavor, want := range tests {
		if got := stripComponentsFor(flavor); got != want {
			t.Errorf("stripComponentsFor(%q) = %d, want %d", flavor, got, want)
		}
	}
}

func TestSumsURLFor(t *testing.T) {
	// arch: fixed geo-mirror sha256sums.txt (ignores the tarball arg).
	if got := sumsURLFor("arch", ArchBootstrapURL); got != archSumsURL {
		t.Errorf("sumsURLFor(arch) = %q, want %q", got, archSumsURL)
	}
	// alpine: per-file .sha256 sidecar.
	wantAlpine := AlpineMinirootURL + ".sha256"
	if got := sumsURLFor("alpine", AlpineMinirootURL); got != wantAlpine {
		t.Errorf("sumsURLFor(alpine) = %q, want %q", got, wantAlpine)
	}
	// unknown: empty (fail closed).
	if got := sumsURLFor("unknown", "https://x/y.tar.gz"); got != "" {
		t.Errorf("sumsURLFor(unknown) = %q, want empty", got)
	}

	// debian without the env set: empty (forces fail-closed unless
	// insecure-skip is opted into elsewhere).
	t.Setenv(envDebianRootfsSHA256URL, "")
	if got := sumsURLFor("debian", "ignored"); got != "" {
		t.Errorf("sumsURLFor(debian) with no env = %q, want empty", got)
	}
	// debian WITH the env set: returns it verbatim.
	t.Setenv(envDebianRootfsSHA256URL, "https://example.test/rootfs.tar.xz.sha256")
	if got := sumsURLFor("debian", "ignored"); got != "https://example.test/rootfs.tar.xz.sha256" {
		t.Errorf("sumsURLFor(debian) with env = %q, want the env value", got)
	}
}

func TestTarballURLPerFlavor(t *testing.T) {
	if got := TarballURL("arch"); got != ArchBootstrapURL {
		t.Errorf("TarballURL(arch) = %q, want %q", got, ArchBootstrapURL)
	}
	if got := TarballURL("alpine"); got != AlpineMinirootURL {
		t.Errorf("TarballURL(alpine) = %q, want %q", got, AlpineMinirootURL)
	}
	if got := TarballURL("unknown"); got != "" {
		t.Errorf("TarballURL(unknown) = %q, want empty", got)
	}

	// debian comes from the env var.
	t.Setenv(envDebianRootfsURL, "")
	if got := TarballURL("debian"); got != "" {
		t.Errorf("TarballURL(debian) with no env = %q, want empty", got)
	}
	t.Setenv(envDebianRootfsURL, "https://example.test/debian-rootfs.tar.xz")
	if got := TarballURL("debian"); got != "https://example.test/debian-rootfs.tar.xz" {
		t.Errorf("TarballURL(debian) with env = %q, want the env value", got)
	}
}

func TestResolveTarballURLDebian(t *testing.T) {
	// No env → clear, actionable error (never a silent broken URL).
	t.Setenv(envDebianRootfsURL, "")
	if _, err := resolveTarballURL("debian"); err == nil {
		t.Fatal("expected error for debian with no PEACOCK_DEBIAN_ROOTFS_URL")
	}
	// Env set → returns it.
	t.Setenv(envDebianRootfsURL, "https://example.test/debian-rootfs.tar.xz")
	got, err := resolveTarballURL("debian")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != "https://example.test/debian-rootfs.tar.xz" {
		t.Errorf("resolveTarballURL(debian) = %q, want the env value", got)
	}
}

func TestIsSupportedHostChrootFlavor(t *testing.T) {
	for _, f := range []string{"arch", "debian", "alpine"} {
		if !IsSupportedHostChrootFlavor(f) {
			t.Errorf("expected %q to be supported", f)
		}
	}
	for _, f := range []string{"", "ubuntu", "gentoo", "Arch"} {
		if IsSupportedHostChrootFlavor(f) {
			t.Errorf("expected %q to be unsupported", f)
		}
	}
}
