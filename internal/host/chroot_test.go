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
	// Debian SHA256SUMS form: same shape, sometimes with "./" prefix or
	// a binary-mode "*" marker.
	debianSums := `deadbeefcafe *./debian-12-genericcloud-amd64.tar.xz
1111111111111111  debian-12-nocloud-amd64.tar.xz`
	// Alpine miniroot form.
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
		{"debian star and dotslash", debianSums, "debian-12-genericcloud-amd64.tar.xz", "deadbeefcafe", false},
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
		"debian":  0, // genericcloud extracts flat
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
	tests := []struct {
		flavor  string
		tarball string
		want    string
	}{
		{
			flavor:  "arch",
			tarball: ArchBootstrapURL,
			want:    archSumsURL,
		},
		{
			flavor:  "alpine",
			tarball: AlpineMinirootURL,
			want:    "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/sha256sums.txt",
		},
		{
			flavor:  "debian",
			tarball: DebianRootfsURL,
			want:    "https://cloud.debian.org/images/cloud/bookworm/latest/SHA256SUMS",
		},
		{
			flavor:  "unknown",
			tarball: "https://x/y.tar.gz",
			want:    "",
		},
	}
	for _, tt := range tests {
		t.Run(tt.flavor, func(t *testing.T) {
			if got := sumsURLFor(tt.flavor, tt.tarball); got != tt.want {
				t.Errorf("sumsURLFor(%q, %q) = %q, want %q", tt.flavor, tt.tarball, got, tt.want)
			}
		})
	}
}

func TestTarballURLPerFlavor(t *testing.T) {
	tests := map[string]string{
		"arch":    ArchBootstrapURL,
		"debian":  DebianRootfsURL,
		"alpine":  AlpineMinirootURL,
		"unknown": "",
	}
	for flavor, want := range tests {
		if got := TarballURL(flavor); got != want {
			t.Errorf("TarballURL(%q) = %q, want %q", flavor, got, want)
		}
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
