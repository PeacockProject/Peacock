package builder

// Build-phase model helpers: stage the shared lib/build phase library
// into a build dir and generate the source-and-run driver script that
// replaces inline [build].script. See docs/design/build-phases.md.

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/manifest"
	"peacock/internal/ports"
)

// buildLibDirName is where the shared phase library is staged inside a
// build dir (a hidden sibling of the port files, copied per build — the
// library itself lives once in peacock-ports/lib/build/).
const buildLibDirName = ".peacock-buildlib"

var archiveExts = []string{".tar.gz", ".tgz", ".tar.xz", ".tar.zst", ".tar.bz2", ".tar"}

func hasArchiveExt(name string) bool {
	for _, e := range archiveExts {
		if strings.HasSuffix(name, e) {
			return true
		}
	}
	return false
}

// archiveBasename picks a filename (with a recognizable archive
// extension) to stage the downloaded source under, so default_prepare's
// glob finds it. Prefers the source URL basename, then the cached
// tarball's, then a generic fallback.
func archiveBasename(sourceURL, tarball string) string {
	for _, c := range []string{sourceURL, tarball} {
		if c == "" {
			continue
		}
		b := filepath.Base(c)
		if hasArchiveExt(b) {
			return b
		}
	}
	return "source.tar.gz"
}

func copyFileLocal(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	return out.Close()
}

// stageBuildLib copies peacock-ports/lib/build/*.sh into
// buildDir/.peacock-buildlib so the driver can source them. The library
// is authored once in the repo; this is the same transient per-build
// copy used for a port's own config files.
func stageBuildLib(buildDir string) error {
	root, ok := ports.Resolve()
	if !ok {
		return fmt.Errorf("cannot resolve peacock-ports to stage build library")
	}
	libSrc := filepath.Join(root, "lib", "build")
	entries, err := os.ReadDir(libSrc)
	if err != nil {
		return fmt.Errorf("read %s: %w", libSrc, err)
	}
	libDst := filepath.Join(buildDir, buildLibDirName)
	if err := os.MkdirAll(libDst, 0755); err != nil {
		return err
	}
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".sh") {
			continue
		}
		if err := copyFileLocal(filepath.Join(libSrc, e.Name()), filepath.Join(libDst, e.Name())); err != nil {
			return err
		}
	}
	return nil
}

// shQuote single-quotes a value for safe shell assignment.
func shQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
}

// newModelScript generates the driver: an env/var preamble, then sources
// the shared default + build_type libs, then the port's build.sh (whose
// functions override defaults), then runs the phases.
func newModelScript(pkg *manifest.Package, buildType string, hasBuildSh bool) string {
	var b strings.Builder
	b.WriteString("#!/bin/sh\nset -e\n")
	b.WriteString("pkgname=" + shQuote(pkg.Package.Name) + "\n")
	b.WriteString("pkgver=" + shQuote(pkg.Package.Version) + "\n")
	b.WriteString("srcdir=\"$PWD\"\n")
	b.WriteString("builddir=\"$PWD\"\n")
	b.WriteString("pkgdir=\"$PWD/stage\"\n")
	b.WriteString("jobs=\"${PEACOCK_JOBS:-4}\"\n")

	type kv struct{ name, val string }
	for _, p := range []kv{
		{"prefix", pkg.Build.Prefix},
		{"configure_args", pkg.Build.ConfigureArgs},
		{"make_args", pkg.Build.MakeArgs},
		{"make_install_args", pkg.Build.MakeInstallArgs},
		{"patches", pkg.Build.Patches},
		{"strip", pkg.Build.Strip},
	} {
		if p.val != "" {
			b.WriteString(p.name + "=" + shQuote(p.val) + "\n")
		}
	}

	b.WriteString(". ./" + buildLibDirName + "/default.sh\n")
	b.WriteString(". ./" + buildLibDirName + "/" + buildType + ".sh\n")
	if hasBuildSh {
		b.WriteString("[ -f ./build.sh ] && . ./build.sh\n")
	}
	b.WriteString("run_phases\n")
	return b.String()
}
