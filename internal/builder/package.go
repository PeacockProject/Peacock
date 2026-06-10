package builder

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/manifest"
)

// featherManifest renders the manifest.toml that goes at the root of a
// .feather archive, from the port's metadata. ftr requires [package].name,
// [package].version, and [install].layout; the rest is optional.
func featherManifest(pkg *manifest.Package, version string) string {
	var b strings.Builder
	b.WriteString("[package]\n")
	fmt.Fprintf(&b, "name = %q\n", pkg.Package.Name)
	fmt.Fprintf(&b, "version = %q\n", version)
	if pkg.Package.Description != "" {
		fmt.Fprintf(&b, "description = %q\n", pkg.Package.Description)
	}
	if rt := pkg.Package.Runtime; rt != "" {
		fmt.Fprintf(&b, "runtime = %q\n", rt)
	}
	if deps := nonEmpty(pkg.Package.Depends); len(deps) > 0 {
		b.WriteString("depends = [")
		for i, d := range deps {
			if i > 0 {
				b.WriteString(", ")
			}
			fmt.Fprintf(&b, "%q", d)
		}
		b.WriteString("]\n")
	}

	b.WriteString("\n[install]\n")
	fmt.Fprintf(&b, "layout = %q\n", pkg.ResolvedLayout())
	if pkg.Install.Prefix != "" {
		fmt.Fprintf(&b, "prefix = %q\n", pkg.Install.Prefix)
	}

	if len(pkg.Provides) > 0 {
		b.WriteString("\n[provides]\n")
		for cap, ver := range pkg.Provides {
			fmt.Fprintf(&b, "%q = %q\n", cap, ver)
		}
	}
	if len(pkg.Conflicts) > 0 {
		b.WriteString("\n[conflicts]\n")
		for cap, ver := range pkg.Conflicts {
			fmt.Fprintf(&b, "%q = %q\n", cap, ver)
		}
	}
	return b.String()
}

func nonEmpty(in []string) []string {
	out := in[:0:0]
	for _, s := range in {
		if s != "" {
			out = append(out, s)
		}
	}
	return out
}

// PackageArtifact creates a .feather package (a gzip tarball of
// manifest.toml + the staged tree under files/) in the per-arch package
// store. `ftr install` consumes it. Returns the archive path.
func (b *Builder) PackageArtifact(buildDir string, pkg *manifest.Package, arch string) (string, error) {
	pkgRoot := buildDir
	stageDir := filepath.Join(buildDir, "stage")
	if info, err := os.Stat(stageDir); err == nil && info.IsDir() {
		pkgRoot = stageDir
	}

	// Normalize arch (armv7 -> armv7h).
	pacmanArch := arch
	if arch == "armv7" {
		pacmanArch = "armv7h"
	}

	pkgrel := "1"
	version := fmt.Sprintf("%s-%s", pkg.Package.Version, pkgrel)

	archStoreDir := filepath.Join(b.PackagesDir(), pacmanArch)
	if err := os.MkdirAll(archStoreDir, 0755); err != nil {
		return "", err
	}
	tarPath := filepath.Join(archStoreDir, fmt.Sprintf("%s-%s-%s.feather", pkg.Package.Name, version, pacmanArch))
	file, err := os.Create(tarPath)
	if err != nil {
		return "", err
	}
	defer file.Close()

	gw := gzip.NewWriter(file)
	defer gw.Close()
	tw := tar.NewWriter(gw)
	defer tw.Close()

	// manifest.toml at the archive root.
	manifestBytes := []byte(featherManifest(pkg, version))
	if err := tw.WriteHeader(&tar.Header{
		Name: "manifest.toml",
		Mode: 0644,
		Size: int64(len(manifestBytes)),
	}); err != nil {
		return "", err
	}
	if _, err := tw.Write(manifestBytes); err != nil {
		return "", err
	}

	// files/ — the staged tree feather overlays onto the install prefix.
	if err := tw.WriteHeader(&tar.Header{
		Name:     "files/",
		Typeflag: tar.TypeDir,
		Mode:     0755,
	}); err != nil {
		return "", err
	}

	err = filepath.Walk(pkgRoot, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		relPath, _ := filepath.Rel(pkgRoot, path)
		if relPath == "." {
			return nil
		}
		archiveName := filepath.Join("files", relPath)

		if info.Mode()&os.ModeSymlink != 0 {
			linkTarget, err := os.Readlink(path)
			if err != nil {
				return err
			}
			return tw.WriteHeader(&tar.Header{
				Name:     archiveName,
				Typeflag: tar.TypeSymlink,
				Linkname: linkTarget,
				Mode:     int64(info.Mode().Perm()),
			})
		}

		header, err := tar.FileInfoHeader(info, info.Name())
		if err != nil {
			return err
		}
		header.Name = archiveName
		if info.IsDir() {
			header.Name += "/"
		}
		if err := tw.WriteHeader(header); err != nil {
			return err
		}
		if !info.IsDir() {
			f, err := os.Open(path)
			if err != nil {
				return err
			}
			defer f.Close()
			io.Copy(tw, f)
		}
		return nil
	})

	return tarPath, err
}
