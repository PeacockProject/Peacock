package builder

import (
	"crypto/sha256"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"peacock/internal/manifest"
	"peacock/internal/runner"
)

// Builder handles package building and caching
type Builder struct {
	CacheDir string
}

// NewBuilder creates a new Builder instance
func NewBuilder(cacheDir string) (*Builder, error) {
	if err := os.MkdirAll(cacheDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create cache dir: %w", err)
	}
	return &Builder{CacheDir: cacheDir}, nil
}

// PackagesDir is the per-arch built-package store (the feather-facing
// repo), a sibling of CacheDir: <var>/packages/<arch>/. Built .pkg.tar.gz
// live here; CacheDir holds only source downloads + build-dep staging.
func (b *Builder) PackagesDir() string {
	return filepath.Join(filepath.Dir(b.CacheDir), "packages")
}

// DistroPkgCacheDir is the persistent per-(flavor, arch) download cache for
// the base distro's own packages — pacman *.pkg.tar.zst (kind "pkg") or apk
// *.apk (kind "apk"). It lives beside PackagesDir at
// <var>/distro-cache/<flavor>/<arch>/<kind>/ and is bind-mounted (or passed as
// --cachedir) into build + rootfs chroots, so a fresh build reuses prior
// downloads instead of re-fetching from the distro mirrors. Partitioning by
// arch keeps an aarch64 build from colliding with an armv7h/x86_64 one in a
// single flat dir.
func (b *Builder) DistroPkgCacheDir(flavor, arch string) string {
	if flavor == "" {
		flavor = "arch"
	}
	switch arch {
	case "":
		arch = HostArchString()
	case "armv7":
		arch = "armv7h"
	}
	kind := "pkg"
	if flavor == "alpine" {
		kind = "apk"
	}
	d := filepath.Join(filepath.Dir(b.CacheDir), "distro-cache", flavor, arch, kind)
	_ = os.MkdirAll(d, 0o755)
	return d
}

// sourceCachePath returns the cache path for a source URL. The key is a
// hash of the FULL url (not just the basename) so the many GitHub
// `…/master.tar.gz` sources don't collide on one file — that collision
// previously made a port silently reuse another's cached source. The
// basename is kept as a suffix for readability + the archive extension.
func (b *Builder) sourceCachePath(url string) string {
	h := sha256.Sum256([]byte(url))
	name := fmt.Sprintf("%x-%s", h[:6], filepath.Base(url))
	return filepath.Join(b.CacheDir, "sources", name)
}

// Download fetches a file from url and caches it. Returns path to cached file.
func (b *Builder) Download(url string, expectedChecksum string) (string, error) {
	filename := filepath.Base(url)
	destPath := b.sourceCachePath(url)
	if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
		return "", fmt.Errorf("failed to create source cache dir: %w", err)
	}

	// Check if exists
	if _, err := os.Stat(destPath); err == nil {
		// Validate checksum
		if err := b.verifyChecksum(destPath, expectedChecksum); err == nil {
			runner.Logf("Using cached %s\n", filename)
			return destPath, nil
		}
		runner.Logf("Cached file %s invalid, redownloading...\n", filename)
	}

	// Copy or download source
	if strings.HasPrefix(url, "file://") {
		localPath := strings.TrimPrefix(url, "file://")
		runner.Logf("Copying local file %s...\n", localPath)
		src, err := os.Open(localPath)
		if err != nil {
			return "", fmt.Errorf("failed to open local source: %w", err)
		}
		defer src.Close()
		out, err := os.Create(destPath)
		if err != nil {
			return "", fmt.Errorf("failed to create cache file: %w", err)
		}
		defer out.Close()
		if _, err := io.Copy(out, src); err != nil {
			os.Remove(destPath)
			return "", fmt.Errorf("failed to copy local source: %w", err)
		}
	} else {
		runner.Logf("Downloading %s...\n", url)
		resp, err := http.Get(url)
		if err != nil {
			return "", fmt.Errorf("download failed: %w", err)
		}
		if resp.StatusCode != http.StatusOK {
			return "", fmt.Errorf("download failed with status: %s", resp.Status)
		}
		defer resp.Body.Close()
		out, err := os.Create(destPath)
		if err != nil {
			return "", fmt.Errorf("failed to create file: %w", err)
		}
		defer out.Close()
		if _, err := io.Copy(out, resp.Body); err != nil {
			return "", fmt.Errorf("failed to save file: %w", err)
		}
	}

	// Verify Checksum again
	if err := b.verifyChecksum(destPath, expectedChecksum); err != nil {
		os.Remove(destPath) // Corrupt file
		return "", fmt.Errorf("downloaded file checksum mismatch: %w", err)
	}

	return destPath, nil
}

func (b *Builder) verifyChecksum(path string, expected string) error {
	if expected == "" {
		return nil // No checksum provided (INSECURE but allowed for proto)
	}

	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return err
	}

	sum := fmt.Sprintf("%x", h.Sum(nil))
	if sum != expected {
		return fmt.Errorf("expected %s, got %s", expected, sum)
	}
	return nil
}

// BuildPackage downloads and builds a package based on its manifest
func (b *Builder) BuildPackage(pkg *manifest.Package, targetArch string) (string, error) {
	if pkg.Build.Source == "" {
		return "", fmt.Errorf("package %s has no source URL", pkg.Package.Name)
	}

	// Download Source
	tarball, err := b.Download(pkg.Build.Source, pkg.Build.Checksum)
	if err != nil {
		return "", fmt.Errorf("failed to download source: %w", err)
	}

	// Unpack and Build Dir
	// We append targetArch to build dir to separate builds for different archs
	buildDir := filepath.Join(b.CacheDir, "build", pkg.Package.Name+"-"+pkg.Package.Version+"-"+targetArch)
	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return "", err
	}

	// 0. Copy auxiliary files (config, patches) from package directory
	if pkg.ManifestDir != "" {
		files, err := os.ReadDir(pkg.ManifestDir)
		if err == nil {
			for _, file := range files {
				if file.Name() == "package.toml" {
					continue
				}
				srcFile := filepath.Join(pkg.ManifestDir, file.Name())
				destFile := filepath.Join(buildDir, file.Name())

				// Simple file copy
				in, err := os.Open(srcFile)
				if err != nil {
					continue
				}

				out, err := os.Create(destFile)
				if err != nil {
					in.Close()
					continue
				}

				io.Copy(out, in)
				in.Close()
				out.Close()
			}
		}
	}

	runner.Logf("Building package %s %s for %s in %s...\n", pkg.Package.Name, pkg.Package.Version, targetArch, buildDir)

	// 1. Extract Source
	// Using external tar for simplicity and robustness in diverse environments
	// Check if tarball exists (it should from Download)
	cmd := exec.Command("tar", "-xf", tarball, "-C", buildDir, "--strip-components=1")
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(cmd); err != nil {
		return "", fmt.Errorf("failed to extract source: %w", err)
	}

	// 2. Execute Build Script from Manifest
	if pkg.Build.Script != "" {
		runner.Logln("Running build script...")
		buildCmd := exec.Command("sh", "-c", pkg.Build.Script)
		buildCmd.Dir = buildDir
		buildCmd.Stdout = runner.LogWriter()
		buildCmd.Stderr = runner.LogWriter()

		// Environment for Cross-Compilation
		buildCmd.Env = os.Environ()

		// Simple mapping for prototype
		var arch, crossCompile string
		switch targetArch {
		case "armv7h":
			arch = "arm"
			crossCompile = "arm-none-eabi-" // Or arm-linux-gnueabihf- logic
		case "aarch64":
			arch = "arm64"
			crossCompile = "aarch64-linux-gnu-"
		case "x86_64":
			arch = "x86_64"
			crossCompile = ""
		}

		if arch != "" {
			buildCmd.Env = append(buildCmd.Env, "ARCH="+arch)
			if crossCompile != "" {
				buildCmd.Env = append(buildCmd.Env, "CROSS_COMPILE="+crossCompile)
			}
		}

		if err := runner.RunCmd(buildCmd); err != nil {
			return "", fmt.Errorf("build script failed: %w", err)
		}
	}

	// 3. Artifact Handling
	return buildDir, nil
}
