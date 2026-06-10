// Package toolchain resolves a port's abstract build capabilities (e.g.
// "c-toolchain") into concrete distro packages, per build mode × target
// arch × flavor, using peacock-ports/toolchains.toml. It replaces the
// gcc-<arch> alias-injection shim with a typed, fail-fast model.
//
// See Peacock/docs/design/toolchain-capabilities.md for the design.
package toolchain

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/pelletier/go-toml/v2"

	"peacock/internal/ports"
)

// Root overrides the directory holding toolchains.toml. Empty (default)
// resolves it under the peacock-ports tree. Tests set this to a fixture
// dir.
var Root = ""

// entry is one (capability, mode, flavor) cell: either a package list or
// an explicit unsupported reason. Exactly one should be set.
type entry struct {
	Packages    []string `toml:"packages"`
	Unsupported string   `toml:"unsupported"`
}

// file mirrors toolchains.toml.
//
//	[triples]  arch -> GNU triple
//	[debarch]  arch -> Debian arch tag
//	[capabilities.<name>.<mode>.<flavor>]  -> entry
type file struct {
	Triples      map[string]string                      `toml:"triples"`
	Debarch      map[string]string                      `toml:"debarch"`
	Capabilities map[string]map[string]map[string]entry `toml:"capabilities"`
}

// Resolution is the result of resolving a port's capabilities.
type Resolution struct {
	// Packages to install into the build chroot for the resolved
	// capabilities (in addition to the port's plain build_deps).
	Packages []string
	// CrossCompile is the derived CROSS_COMPILE prefix (e.g.
	// "aarch64-linux-gnu-") when cross-building, else "". Callers honor an
	// explicit port [build].cross_compile over this.
	CrossCompile string
}

// loadFile reads + parses toolchains.toml from the resolved root.
func loadFile() (*file, string, error) {
	root := Root
	if root == "" {
		if r, ok := ports.Resolve(); ok {
			root = r
		} else {
			root = "peacock-ports"
		}
	}
	path := filepath.Join(root, "toolchains.toml")
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, path, fmt.Errorf("toolchain: read %s: %w", path, err)
	}
	var f file
	if err := toml.Unmarshal(data, &f); err != nil {
		return nil, path, fmt.Errorf("toolchain: parse %s: %w", path, err)
	}
	return &f, path, nil
}

// Resolve turns a port's capabilities into concrete packages + a derived
// CROSS_COMPILE. cross selects the cross vs native package set;
// targetArch + tripleOverride determine the {triple}/{debarch}
// substitutions and the derived prefix. flavor is the active base distro.
//
// Every failure (unknown capability, no entry for the flavor, an
// "unsupported" cell, a missing triple/debarch for a referenced token)
// returns an error at this point — before any chroot work — so a bad
// combo fails fast rather than mid-pacman.
func Resolve(capabilities []string, targetArch, tripleOverride, flavor string, cross bool) (Resolution, error) {
	var res Resolution
	if len(capabilities) == 0 {
		// No capabilities: still derive CROSS_COMPILE when cross-building
		// so a port that only declares target_arch gets a sane prefix.
		if cross && targetArch != "" {
			f, _, err := loadFile()
			if err != nil {
				return res, err
			}
			res.CrossCompile = deriveCC(f, targetArch, tripleOverride)
		} else if cross && tripleOverride != "" {
			res.CrossCompile = tripleOverride + "-"
		}
		return res, nil
	}
	if flavor == "" {
		flavor = "arch"
	}

	f, path, err := loadFile()
	if err != nil {
		return res, err
	}

	mode := "native"
	if cross {
		mode = "cross"
	}

	triple := tripleOverride
	if triple == "" && targetArch != "" {
		triple = f.Triples[targetArch]
	}
	debarch := f.Debarch[targetArch]

	for _, cap := range capabilities {
		modes, ok := f.Capabilities[cap]
		if !ok {
			return res, fmt.Errorf("toolchain: unknown capability %q (not in %s)", cap, path)
		}
		flavors, ok := modes[mode]
		if !ok {
			return res, fmt.Errorf("toolchain: capability %q has no %q block in %s", cap, mode, path)
		}
		e, ok := flavors[flavor]
		if !ok {
			return res, fmt.Errorf("toolchain: capability %q (%s) undefined for flavor %q in %s", cap, mode, flavor, path)
		}
		if e.Unsupported != "" {
			return res, fmt.Errorf("toolchain: capability %q (%s/%s) unsupported: %s", cap, mode, flavor, e.Unsupported)
		}
		for _, p := range e.Packages {
			sub, err := substitute(p, triple, debarch, targetArch)
			if err != nil {
				return res, err
			}
			res.Packages = append(res.Packages, sub)
		}
	}

	if cross {
		res.CrossCompile = deriveCC(f, targetArch, tripleOverride)
	}
	return res, nil
}

// deriveCC computes the CROSS_COMPILE prefix from the triple
// (override → table). Returns "" if neither yields a triple.
func deriveCC(f *file, targetArch, tripleOverride string) string {
	t := tripleOverride
	if t == "" && targetArch != "" {
		t = f.Triples[targetArch]
	}
	if t == "" {
		return ""
	}
	return t + "-"
}

// substitute expands {triple} / {debarch} in a package string,
// fail-fast when a referenced token has no value for the arch.
func substitute(pkg, triple, debarch, targetArch string) (string, error) {
	if strings.Contains(pkg, "{triple}") {
		if triple == "" {
			return "", fmt.Errorf("toolchain: %q needs {triple} but no triple for arch %q", pkg, targetArch)
		}
		pkg = strings.ReplaceAll(pkg, "{triple}", triple)
	}
	if strings.Contains(pkg, "{debarch}") {
		if debarch == "" {
			return "", fmt.Errorf("toolchain: %q needs {debarch} but no debarch for arch %q", pkg, targetArch)
		}
		pkg = strings.ReplaceAll(pkg, "{debarch}", debarch)
	}
	return pkg, nil
}
