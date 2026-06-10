package pipeline

import (
	"archive/tar"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"peacock/internal/builder"
	"peacock/internal/manifest"
	"peacock/internal/runner"
)

// touch creates an empty file (and its parent dirs) under a fixture tree.
func touch(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, nil, 0o644); err != nil {
		t.Fatal(err)
	}
}

// writePkgTarball writes a minimal pacman-style .pkg.tar.gz whose only
// entry is .PKGINFO with the given contents.
func writePkgTarball(t *testing.T, path, pkginfo string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	gz := gzip.NewWriter(f)
	tw := tar.NewWriter(gz)
	if err := tw.WriteHeader(&tar.Header{Name: ".PKGINFO", Mode: 0o644, Size: int64(len(pkginfo))}); err != nil {
		t.Fatal(err)
	}
	if _, err := io.WriteString(tw, pkginfo); err != nil {
		t.Fatal(err)
	}
	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	if err := gz.Close(); err != nil {
		t.Fatal(err)
	}
}

// silenceRunnerLog routes runner log output into the void for the test
// so helpers that report cache misses don't spam `go test` output.
func silenceRunnerLog(t *testing.T) {
	t.Helper()
	prev := runner.LogWriter()
	runner.SetLogWriter(io.Discard)
	t.Cleanup(func() { runner.SetLogWriter(prev) })
}

func TestPacmanArch(t *testing.T) {
	cases := []struct{ in, want string }{
		{"armv7", "armv7h"},
		{"aarch64", "aarch64"},
		{"x86_64", "x86_64"},
		{"", ""},
	}
	for _, tc := range cases {
		if got := pacmanArch(tc.in); got != tc.want {
			t.Errorf("pacmanArch(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestParseUseQemu(t *testing.T) {
	cases := []struct {
		in      string
		want    *bool // nil means "auto"
		wantErr bool
	}{
		{in: "auto", want: nil},
		{in: "", want: nil},
		{in: "true", want: boolPtr(true)},
		{in: "false", want: boolPtr(false)},
		{in: "yes", wantErr: true},
		{in: "TRUE", wantErr: true},
	}
	for _, tc := range cases {
		got, err := parseUseQemu(tc.in)
		if tc.wantErr {
			if err == nil {
				t.Errorf("parseUseQemu(%q) = %v, want error", tc.in, got)
			}
			continue
		}
		if err != nil {
			t.Errorf("parseUseQemu(%q) error = %v", tc.in, err)
			continue
		}
		switch {
		case tc.want == nil && got != nil:
			t.Errorf("parseUseQemu(%q) = %v, want nil", tc.in, *got)
		case tc.want != nil && (got == nil || *got != *tc.want):
			t.Errorf("parseUseQemu(%q) = %v, want %v", tc.in, got, *tc.want)
		}
	}
}

func TestDtbPreferenceTokens(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want []string
	}{
		{"empty", "", nil},
		{"whitespace only", "   ", nil},
		{"hyphenated device keeps full name last", "oppo-a16", []string{"oppo", "a16", "oppo-a16"}},
		{"case folded + dedup", "Oppo-OPPO-a16", []string{"oppo", "a16", "oppo-oppo-a16"}},
		{"single token keeps itself once", "peacock", []string{"peacock"}},
		{"underscores split too", "mt6765_peacock", []string{"mt6765", "peacock", "mt6765_peacock"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := dtbPreferenceTokens(tc.in)
			if !reflect.DeepEqual(got, tc.want) {
				t.Fatalf("dtbPreferenceTokens(%q) = %v, want %v", tc.in, got, tc.want)
			}
		})
	}
}

func TestDecodeMountInfoPath(t *testing.T) {
	cases := []struct{ in, want string }{
		{`/plain/path`, `/plain/path`},
		{`/with\040space`, `/with space`},
		{`/tab\011here`, "/tab\there"},
		{`/multi\040word\040dir`, `/multi word dir`},
		{`/trailing\04`, `/trailing\04`},   // incomplete escape passes through
		{`/not\998octal`, `/not\998octal`}, // non-octal digits pass through
		{``, ``},
	}
	for _, tc := range cases {
		if got := decodeMountInfoPath(tc.in); got != tc.want {
			t.Errorf("decodeMountInfoPath(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestKernelArtifactExists(t *testing.T) {
	relCandidates := []string{
		"zImage",
		"Image.gz",
		"Image",
		"arch/arm/boot/zImage",
		"arch/arm64/boot/Image.gz",
		"arch/arm64/boot/Image",
	}
	for _, rel := range relCandidates {
		t.Run(rel, func(t *testing.T) {
			dir := t.TempDir()
			touch(t, filepath.Join(dir, rel))
			if !kernelArtifactExists(dir) {
				t.Fatalf("kernelArtifactExists(%s with %s) = false, want true", dir, rel)
			}
		})
	}

	t.Run("empty build dir string", func(t *testing.T) {
		if kernelArtifactExists("") {
			t.Fatal("kernelArtifactExists(\"\") = true, want false")
		}
	})
	t.Run("no artifacts", func(t *testing.T) {
		if kernelArtifactExists(t.TempDir()) {
			t.Fatal("kernelArtifactExists(empty dir) = true, want false")
		}
	})
	t.Run("directory named like artifact does not count", func(t *testing.T) {
		dir := t.TempDir()
		if err := os.MkdirAll(filepath.Join(dir, "zImage"), 0o755); err != nil {
			t.Fatal(err)
		}
		if kernelArtifactExists(dir) {
			t.Fatal("kernelArtifactExists(dir with zImage/ dir) = true, want false")
		}
	})
}

func TestDiscoverKernelDTB(t *testing.T) {
	t.Run("empty kernel dir", func(t *testing.T) {
		if got := discoverKernelDTB("", "oppo-a16"); got != "" {
			t.Fatalf("discoverKernelDTB(\"\") = %q, want \"\"", got)
		}
	})

	t.Run("no dtbs", func(t *testing.T) {
		if got := discoverKernelDTB(t.TempDir(), "oppo-a16"); got != "" {
			t.Fatalf("discoverKernelDTB(empty) = %q, want \"\"", got)
		}
	})

	t.Run("prefers device-token match over first hit", func(t *testing.T) {
		dir := t.TempDir()
		other := filepath.Join(dir, "dtbs", "aaa-generic.dtb")
		want := filepath.Join(dir, "arch", "arm64", "boot", "dts", "mt6765-oppo-a16.dtb")
		touch(t, other)
		touch(t, want)
		if got := discoverKernelDTB(dir, "oppo-a16"); got != want {
			t.Fatalf("discoverKernelDTB = %q, want %q", got, want)
		}
	})

	t.Run("falls back to first dtb when nothing matches", func(t *testing.T) {
		dir := t.TempDir()
		want := filepath.Join(dir, "dtbs", "generic-board.dtb")
		touch(t, want)
		if got := discoverKernelDTB(dir, "oppo-a16"); got != want {
			t.Fatalf("discoverKernelDTB = %q, want %q", got, want)
		}
	})

	t.Run("more matching tokens wins", func(t *testing.T) {
		dir := t.TempDir()
		weak := filepath.Join(dir, "dtbs", "oppo-other.dtb")
		strong := filepath.Join(dir, "dtbs", "oppo-a16.dtb")
		touch(t, weak)
		touch(t, strong)
		if got := discoverKernelDTB(dir, "oppo-a16"); got != strong {
			t.Fatalf("discoverKernelDTB = %q, want %q", got, strong)
		}
	})

	t.Run("non-dtb files ignored", func(t *testing.T) {
		dir := t.TempDir()
		touch(t, filepath.Join(dir, "dtbs", "oppo-a16.dts"))
		if got := discoverKernelDTB(dir, "oppo-a16"); got != "" {
			t.Fatalf("discoverKernelDTB = %q, want \"\"", got)
		}
	})
}

func TestCachedArtifactPath(t *testing.T) {
	t.Run("miss returns empty", func(t *testing.T) {
		if got := cachedArtifactPath(t.TempDir(), "foo", "1.0", "x86_64"); got != "" {
			t.Fatalf("cachedArtifactPath = %q, want \"\"", got)
		}
	})

	// pkgArch returns the per-arch package store dir for a fixture cacheDir.
	pkgArch := func(cacheDir, arch string) string {
		return filepath.Join(packagesStoreDir(cacheDir), arch)
	}

	t.Run("hit in per-arch package store", func(t *testing.T) {
		dir := t.TempDir()
		want := filepath.Join(pkgArch(dir, "x86_64"), "foo-1.0-1-x86_64.pkg.tar.gz")
		touch(t, want)
		if got := cachedArtifactPath(dir, "foo", "1.0", "x86_64"); got != want {
			t.Fatalf("cachedArtifactPath = %q, want %q", got, want)
		}
	})

	t.Run("armv7 prefers pacman arch armv7h", func(t *testing.T) {
		dir := t.TempDir()
		want := filepath.Join(pkgArch(dir, "armv7h"), "foo-1.0-1-armv7h.pkg.tar.gz")
		plain := filepath.Join(pkgArch(dir, "armv7"), "foo-1.0-1-armv7.pkg.tar.gz")
		touch(t, want)
		touch(t, plain)
		if got := cachedArtifactPath(dir, "foo", "1.0", "armv7"); got != want {
			t.Fatalf("cachedArtifactPath = %q, want %q", got, want)
		}
	})

	t.Run("falls back to raw arch name dir", func(t *testing.T) {
		dir := t.TempDir()
		want := filepath.Join(pkgArch(dir, "armv7"), "foo-1.0-1-armv7.pkg.tar.gz")
		touch(t, want)
		if got := cachedArtifactPath(dir, "foo", "1.0", "armv7"); got != want {
			t.Fatalf("cachedArtifactPath = %q, want %q", got, want)
		}
	})

	t.Run("legacy flat cache migrates to package store", func(t *testing.T) {
		dir := t.TempDir()
		legacy := filepath.Join(dir, "foo-1.0-x86_64.pkg.tar.gz")
		touch(t, legacy)
		want := filepath.Join(pkgArch(dir, "x86_64"), "foo-1.0-1-x86_64.pkg.tar.gz")
		if got := cachedArtifactPath(dir, "foo", "1.0", "x86_64"); got != want {
			t.Fatalf("cachedArtifactPath = %q, want migrated %q", got, want)
		}
		if _, err := os.Stat(legacy); !os.IsNotExist(err) {
			t.Fatalf("legacy file %s still present after migration", legacy)
		}
		if _, err := os.Stat(want); err != nil {
			t.Fatalf("migrated file %s missing: %v", want, err)
		}
	})
}

func TestLocalPackageManifestPath(t *testing.T) {
	setupPorts := func(t *testing.T, rels ...string) {
		t.Helper()
		dir := t.TempDir()
		for _, rel := range rels {
			touch(t, filepath.Join(dir, rel))
		}
		t.Chdir(dir)
	}

	t.Run("miss", func(t *testing.T) {
		setupPorts(t)
		if path, ok := LocalPackageManifestPath("nope"); ok || path != "" {
			t.Fatalf("LocalPackageManifestPath = (%q, %v), want (\"\", false)", path, ok)
		}
	})

	t.Run("device hit", func(t *testing.T) {
		setupPorts(t, "peacock-ports/device/linux-oppo-a16/package.toml")
		path, ok := LocalPackageManifestPath("linux-oppo-a16")
		want := filepath.Join("peacock-ports", "device", "linux-oppo-a16", "package.toml")
		if !ok || path != want {
			t.Fatalf("LocalPackageManifestPath = (%q, %v), want (%q, true)", path, ok, want)
		}
	})

	t.Run("base hit", func(t *testing.T) {
		setupPorts(t, "peacock-ports/base/busybox/package.toml")
		path, ok := LocalPackageManifestPath("busybox")
		want := filepath.Join("peacock-ports", "base", "busybox", "package.toml")
		if !ok || path != want {
			t.Fatalf("LocalPackageManifestPath = (%q, %v), want (%q, true)", path, ok, want)
		}
	})

	t.Run("device wins over base", func(t *testing.T) {
		setupPorts(t,
			"peacock-ports/device/dual/package.toml",
			"peacock-ports/base/dual/package.toml",
		)
		path, ok := LocalPackageManifestPath("dual")
		want := filepath.Join("peacock-ports", "device", "dual", "package.toml")
		if !ok || path != want {
			t.Fatalf("LocalPackageManifestPath = (%q, %v), want (%q, true)", path, ok, want)
		}
	})
}

func TestPackageArchMatches(t *testing.T) {
	cases := []struct {
		name    string
		pkginfo string
		arch    string
		want    bool
	}{
		{
			name:    "matching arch and pkgver-rel",
			pkginfo: "pkgname = foo\npkgver = 1.0-1\narch = x86_64\n",
			arch:    "x86_64",
			want:    true,
		},
		{
			name:    "wrong arch",
			pkginfo: "pkgname = foo\npkgver = 1.0-1\narch = aarch64\n",
			arch:    "x86_64",
			want:    false,
		},
		{
			name:    "legacy pkgrel line rejects",
			pkginfo: "pkgname = foo\npkgver = 1.0-1\npkgrel = 1\narch = x86_64\n",
			arch:    "x86_64",
			want:    false,
		},
		{
			name:    "pkgver without -1 suffix rejects",
			pkginfo: "pkgname = foo\npkgver = 1.0\narch = x86_64\n",
			arch:    "x86_64",
			want:    false,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			path := filepath.Join(t.TempDir(), "pkg.tar.gz")
			writePkgTarball(t, path, tc.pkginfo)
			if got := packageArchMatches(path, tc.arch); got != tc.want {
				t.Fatalf("packageArchMatches = %v, want %v", got, tc.want)
			}
		})
	}

	t.Run("missing file", func(t *testing.T) {
		if packageArchMatches(filepath.Join(t.TempDir(), "absent.pkg.tar.gz"), "x86_64") {
			t.Fatal("packageArchMatches(absent) = true, want false")
		}
	})
	t.Run("not a gzip", func(t *testing.T) {
		path := filepath.Join(t.TempDir(), "garbage.pkg.tar.gz")
		if err := os.WriteFile(path, []byte("not gzip"), 0o644); err != nil {
			t.Fatal(err)
		}
		if packageArchMatches(path, "x86_64") {
			t.Fatal("packageArchMatches(garbage) = true, want false")
		}
	})
	t.Run("tarball without .PKGINFO", func(t *testing.T) {
		path := filepath.Join(t.TempDir(), "nopkginfo.pkg.tar.gz")
		f, err := os.Create(path)
		if err != nil {
			t.Fatal(err)
		}
		gz := gzip.NewWriter(f)
		tw := tar.NewWriter(gz)
		if err := tw.WriteHeader(&tar.Header{Name: "other", Mode: 0o644, Size: 0}); err != nil {
			t.Fatal(err)
		}
		if err := tw.Close(); err != nil {
			t.Fatal(err)
		}
		if err := gz.Close(); err != nil {
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
		if packageArchMatches(path, "x86_64") {
			t.Fatal("packageArchMatches(no .PKGINFO) = true, want false")
		}
	})
}

func TestFindCachedPackageArtifact(t *testing.T) {
	silenceRunnerLog(t)

	newPkg := func(name, version string) *manifest.Package {
		var p manifest.Package
		p.Package.Name = name
		p.Package.Version = version
		return &p
	}

	t.Run("no artifact in cache", func(t *testing.T) {
		b := &builder.Builder{CacheDir: t.TempDir()}
		if got := FindCachedPackageArtifact(b, newPkg("foo", "1.0"), "x86_64"); got != "" {
			t.Fatalf("FindCachedPackageArtifact = %q, want \"\"", got)
		}
	})

	pkgStore := func(cache, arch, file string) string {
		return filepath.Join(packagesStoreDir(cache), arch, file)
	}

	t.Run("valid artifact resolves", func(t *testing.T) {
		cache := t.TempDir()
		want := pkgStore(cache, "x86_64", "foo-1.0-1-x86_64.pkg.tar.gz")
		writePkgTarball(t, want, "pkgname = foo\npkgver = 1.0-1\narch = x86_64\n")
		b := &builder.Builder{CacheDir: cache}
		if got := FindCachedPackageArtifact(b, newPkg("foo", "1.0"), "x86_64"); got != want {
			t.Fatalf("FindCachedPackageArtifact = %q, want %q", got, want)
		}
	})

	t.Run("armv7 artifact must embed pacman arch armv7h", func(t *testing.T) {
		cache := t.TempDir()
		want := pkgStore(cache, "armv7h", "foo-1.0-1-armv7h.pkg.tar.gz")
		writePkgTarball(t, want, "pkgname = foo\npkgver = 1.0-1\narch = armv7h\n")
		b := &builder.Builder{CacheDir: cache}
		if got := FindCachedPackageArtifact(b, newPkg("foo", "1.0"), "armv7"); got != want {
			t.Fatalf("FindCachedPackageArtifact = %q, want %q", got, want)
		}
	})

	t.Run("arch mismatch forces rebuild", func(t *testing.T) {
		cache := t.TempDir()
		stale := pkgStore(cache, "x86_64", "foo-1.0-1-x86_64.pkg.tar.gz")
		writePkgTarball(t, stale, "pkgname = foo\npkgver = 1.0-1\narch = aarch64\n")
		b := &builder.Builder{CacheDir: cache}
		if got := FindCachedPackageArtifact(b, newPkg("foo", "1.0"), "x86_64"); got != "" {
			t.Fatalf("FindCachedPackageArtifact = %q, want \"\" for mismatched arch", got)
		}
	})

	t.Run("version mismatch misses", func(t *testing.T) {
		cache := t.TempDir()
		old := pkgStore(cache, "x86_64", "foo-0.9-1-x86_64.pkg.tar.gz")
		writePkgTarball(t, old, "pkgname = foo\npkgver = 0.9-1\narch = x86_64\n")
		b := &builder.Builder{CacheDir: cache}
		if got := FindCachedPackageArtifact(b, newPkg("foo", "1.0"), "x86_64"); got != "" {
			t.Fatalf("FindCachedPackageArtifact = %q, want \"\" for version mismatch", got)
		}
	})
}

func TestResolveBuildOptions(t *testing.T) {
	host := builder.HostArchString()
	foreign := "aarch64"
	if host == "aarch64" {
		foreign = "x86_64"
	}

	newPkg := func(mutate func(*manifest.Package)) *manifest.Package {
		var p manifest.Package
		p.Package.Name = "foo"
		p.Package.Version = "1.0"
		if mutate != nil {
			mutate(&p)
		}
		return &p
	}

	t.Run("invalid use-qemu flag errors", func(t *testing.T) {
		if _, _, err := resolveBuildOptions(newPkg(nil), host, "maybe", ""); err == nil {
			t.Fatal("resolveBuildOptions(use-qemu=maybe) = nil error, want error")
		}
	})

	t.Run("native target defaults to no qemu in host chroot", func(t *testing.T) {
		opts, chrootArch, err := resolveBuildOptions(newPkg(nil), host, "auto", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || *opts.UseQemu {
			t.Fatalf("UseQemu = %v, want false", opts.UseQemu)
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want host %q", chrootArch, host)
		}
	})

	t.Run("foreign target defaults to qemu in target chroot", func(t *testing.T) {
		opts, chrootArch, err := resolveBuildOptions(newPkg(nil), foreign, "auto", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || !*opts.UseQemu {
			t.Fatalf("UseQemu = %v, want true", opts.UseQemu)
		}
		if chrootArch != foreign {
			t.Fatalf("chrootArch = %q, want %q", chrootArch, foreign)
		}
	})

	t.Run("cross-compile flag disables qemu and uses host chroot", func(t *testing.T) {
		opts, chrootArch, err := resolveBuildOptions(newPkg(nil), foreign, "auto", "arm-none-eabi-")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || *opts.UseQemu {
			t.Fatalf("UseQemu = %v, want false", opts.UseQemu)
		}
		if opts.CrossCompile != "arm-none-eabi-" {
			t.Fatalf("CrossCompile = %q, want flag value", opts.CrossCompile)
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want host %q", chrootArch, host)
		}
	})

	t.Run("cross-compile flag overrides manifest value", func(t *testing.T) {
		pkg := newPkg(func(p *manifest.Package) { p.Build.CrossCompile = "from-manifest-" })
		opts, _, err := resolveBuildOptions(pkg, foreign, "auto", "from-flag-")
		if err != nil {
			t.Fatal(err)
		}
		if opts.CrossCompile != "from-flag-" {
			t.Fatalf("CrossCompile = %q, want %q", opts.CrossCompile, "from-flag-")
		}
	})

	t.Run("manifest cross-compile used when flag empty", func(t *testing.T) {
		pkg := newPkg(func(p *manifest.Package) { p.Build.CrossCompile = "from-manifest-" })
		opts, chrootArch, err := resolveBuildOptions(pkg, foreign, "auto", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.CrossCompile != "from-manifest-" {
			t.Fatalf("CrossCompile = %q, want %q", opts.CrossCompile, "from-manifest-")
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want host %q (cross-compile implies no qemu)", chrootArch, host)
		}
	})

	t.Run("flag true forces qemu even for native target", func(t *testing.T) {
		opts, chrootArch, err := resolveBuildOptions(newPkg(nil), host, "true", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || !*opts.UseQemu {
			t.Fatalf("UseQemu = %v, want true", opts.UseQemu)
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want %q", chrootArch, host)
		}
	})

	t.Run("flag false overrides manifest qemu preference", func(t *testing.T) {
		pkg := newPkg(func(p *manifest.Package) { p.Build.UseQemu = boolPtr(true) })
		opts, chrootArch, err := resolveBuildOptions(pkg, foreign, "false", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || *opts.UseQemu {
			t.Fatalf("UseQemu = %v, want false", opts.UseQemu)
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want host %q", chrootArch, host)
		}
	})

	t.Run("manifest qemu preference used on auto", func(t *testing.T) {
		pkg := newPkg(func(p *manifest.Package) { p.Build.UseQemu = boolPtr(false) })
		opts, chrootArch, err := resolveBuildOptions(pkg, foreign, "auto", "")
		if err != nil {
			t.Fatal(err)
		}
		if opts.UseQemu == nil || *opts.UseQemu {
			t.Fatalf("UseQemu = %v, want false", opts.UseQemu)
		}
		if chrootArch != host {
			t.Fatalf("chrootArch = %q, want host %q", chrootArch, host)
		}
	})
}

func TestMountPointsUnder(t *testing.T) {
	// Nothing is ever mounted under a fresh TempDir, so this exercises
	// the parse-and-filter path hermetically against the live mountinfo.
	t.Run("no mounts under temp roots", func(t *testing.T) {
		got, err := mountPointsUnder([]string{t.TempDir(), t.TempDir()})
		if err != nil {
			t.Fatal(err)
		}
		if len(got) != 0 {
			t.Fatalf("mountPointsUnder(tempdirs) = %v, want empty", got)
		}
	})

	// Degenerate roots ("", ".", "/") are skipped rather than matching
	// the entire mount table.
	t.Run("degenerate roots ignored", func(t *testing.T) {
		got, err := mountPointsUnder([]string{"", "."})
		if err != nil {
			t.Fatal(err)
		}
		if len(got) != 0 {
			t.Fatalf("mountPointsUnder(degenerate) = %v, want empty", got)
		}
	})
}

func TestLocatePeacockMkinitfs(t *testing.T) {
	cases := []struct {
		name string
		rels []string // files to create; first listed in priority order wins
		want string   // relative path expected back
	}{
		{
			name: "prefers usr/bin over stage and top-level",
			rels: []string{"usr/bin/peacock-mkinitfs", "stage/usr/bin/peacock-mkinitfs", "peacock-mkinitfs"},
			want: "usr/bin/peacock-mkinitfs",
		},
		{
			name: "stage layout next",
			rels: []string{"stage/usr/bin/peacock-mkinitfs", "peacock-mkinitfs"},
			want: "stage/usr/bin/peacock-mkinitfs",
		},
		{
			name: "top-level makefile output last",
			rels: []string{"peacock-mkinitfs"},
			want: "peacock-mkinitfs",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			for _, rel := range tc.rels {
				touch(t, filepath.Join(dir, rel))
			}
			want := filepath.Join(dir, filepath.FromSlash(tc.want))
			if got := locatePeacockMkinitfs(dir); got != want {
				t.Fatalf("locatePeacockMkinitfs = %q, want %q", got, want)
			}
		})
	}
	// The miss case falls through to exec.LookPath, which depends on the
	// host PATH, so it is deliberately not asserted here.
}

func TestFindPaths(t *testing.T) {
	root := t.TempDir()
	touch(t, filepath.Join(root, "usr", "local", "bin", "tool"))
	touch(t, filepath.Join(root, "opt", "thing", "bin", "other"))
	touch(t, filepath.Join(root, "usr", "share", "doc", "readme"))

	got, err := findPaths(root, map[string]struct{}{"bin": {}})
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]bool{
		filepath.Join(root, "usr", "local", "bin"): true,
		filepath.Join(root, "opt", "thing", "bin"): true,
	}
	if len(got) != len(want) {
		t.Fatalf("findPaths = %v, want keys %v", got, want)
	}
	for _, p := range got {
		if !want[p] {
			t.Fatalf("findPaths returned unexpected path %q (all: %v)", p, got)
		}
	}
	if strings.Contains(strings.Join(got, " "), "doc") {
		t.Fatalf("findPaths matched non-bin dir: %v", got)
	}
}
