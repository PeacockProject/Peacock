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
func featherManifest(name, version string, pkg *manifest.Package) string {
	var b strings.Builder
	b.WriteString("[package]\n")
	fmt.Fprintf(&b, "name = %q\n", name)
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

func isDir(p string) bool {
	info, err := os.Stat(p)
	return err == nil && info.IsDir()
}

// PackageArtifact creates the .feather package(s) for a built port in the
// per-arch store and returns the main package path. A kernel port that
// declares prp_kernel_config additionally emits a `<name>-prp` subpackage
// from buildDir/stage-prp — the PRP-trimmed kernel, a build dependency of
// PRP recovery images that is never shipped in the OS rootfs.
func (b *Builder) PackageArtifact(buildDir string, pkg *manifest.Package, arch string) (string, error) {
	pacmanArch := arch
	if arch == "armv7" {
		pacmanArch = "armv7h"
	}
	version := fmt.Sprintf("%s-1", pkg.Package.Version)

	store := filepath.Join(b.PackagesDir(), pacmanArch)
	if err := os.MkdirAll(store, 0755); err != nil {
		return "", err
	}

	mainStage := buildDir
	if d := filepath.Join(buildDir, "stage"); isDir(d) {
		mainStage = d
	}
	mainOut := filepath.Join(store, fmt.Sprintf("%s-%s-%s.feather", pkg.Package.Name, version, pacmanArch))
	hooksDir := filepath.Join(pkg.ManifestDir, "hooks")
	if err := writeFeatherArchive(mainStage, featherManifest(pkg.Package.Name, version, pkg), mainOut, hooksDir); err != nil {
		return "", err
	}

	if pkg.Build.PRPKernelConfig != "" {
		if prpStage := filepath.Join(buildDir, "stage-prp"); isDir(prpStage) {
			prpName := pkg.Package.Name + "-prp"
			prpOut := filepath.Join(store, fmt.Sprintf("%s-%s-%s.feather", prpName, version, pacmanArch))
			if err := writeFeatherArchive(prpStage, featherManifest(prpName, version, pkg), prpOut, ""); err != nil {
				return "", err
			}
		}
	}

	return mainOut, nil
}

// writeFeatherArchive writes a .feather (gzip tar of manifest.toml + the
// stageDir tree under files/) to outPath.
func writeFeatherArchive(stageDir, manifestContent, outPath, hooksDir string) error {
	file, err := os.Create(outPath)
	if err != nil {
		return err
	}
	defer file.Close()
	gw := gzip.NewWriter(file)
	defer gw.Close()
	tw := tar.NewWriter(gw)
	defer tw.Close()

	manifestBytes := []byte(manifestContent)
	if err := tw.WriteHeader(&tar.Header{Name: "manifest.toml", Mode: 0644, Size: int64(len(manifestBytes))}); err != nil {
		return err
	}
	if _, err := tw.Write(manifestBytes); err != nil {
		return err
	}

	// Optional hooks/{pre,post-install}.sh shipped in the port dir — feather
	// runs them on install. Packed (executable) before files/.
	if hooksDir != "" {
		entries, _ := os.ReadDir(hooksDir)
		wroteDir := false
		for _, e := range entries {
			if e.IsDir() || !strings.HasSuffix(e.Name(), ".sh") {
				continue
			}
			data, err := os.ReadFile(filepath.Join(hooksDir, e.Name()))
			if err != nil {
				return err
			}
			if !wroteDir {
				if err := tw.WriteHeader(&tar.Header{Name: "hooks/", Typeflag: tar.TypeDir, Mode: 0755}); err != nil {
					return err
				}
				wroteDir = true
			}
			if err := tw.WriteHeader(&tar.Header{Name: "hooks/" + e.Name(), Mode: 0755, Size: int64(len(data))}); err != nil {
				return err
			}
			if _, err := tw.Write(data); err != nil {
				return err
			}
		}
	}

	if err := tw.WriteHeader(&tar.Header{Name: "files/", Typeflag: tar.TypeDir, Mode: 0755}); err != nil {
		return err
	}

	return filepath.Walk(stageDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		relPath, _ := filepath.Rel(stageDir, path)
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
}
