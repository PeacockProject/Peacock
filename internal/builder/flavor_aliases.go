package builder

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"

	"github.com/pelletier/go-toml/v2"

	"peacock/internal/ports"
	"peacock/internal/runner"
)

// FlavorAliasesRoot overrides the search root for per-flavor alias
// tables. Empty (the default) means "look under ./peacock-ports". Tests
// flip this to point at a hermetic testdata directory; the build path
// never sets it.
var FlavorAliasesRoot = ""

// aliasTableFile is the per-flavor build_deps alias map. Schema:
//
//	[aliases]
//	qt6-base = "libqt6base6-dev"
//	libssl   = "libssl-dev"
//
// Absent file = identity map + first-call warning. Present-but-empty =
// identity map, no warning.
type aliasTableFile struct {
	Aliases map[string]string `toml:"aliases"`
}

var (
	aliasCacheMu sync.Mutex
	aliasCache   = map[string]map[string]string{} // flavor -> map; nil entry => already-warned-missing
)

// flavorAliasesPath returns the canonical location for a flavor's
// alias table. FlavorAliasesRoot lets tests redirect the lookup.
func flavorAliasesPath(flavor string) string {
	root := FlavorAliasesRoot
	if root == "" {
		// Read-only path: use the resolved ports tree when one exists,
		// else fall back to the bare relative dir so callers invoked
		// outside a build (and tests that don't set FlavorAliasesRoot)
		// keep their prior behavior.
		if r, ok := ports.Resolve(); ok {
			root = r
		} else {
			root = "peacock-ports"
		}
	}
	return filepath.Join(root, "flavors", flavor, "aliases.toml")
}

// ResetAliasCache wipes the package-level alias cache so tests can
// observe alias-table changes between sub-tests. Not part of the
// public CLI surface — exported for test packages only.
func ResetAliasCache() {
	aliasCacheMu.Lock()
	defer aliasCacheMu.Unlock()
	aliasCache = map[string]map[string]string{}
}

// loadFlavorAliases returns the rewrite map for the given flavor,
// caching the result. A missing file is not an error: we log a warning
// once per flavor and cache nil so subsequent calls stay quiet.
func loadFlavorAliases(flavor string) map[string]string {
	aliasCacheMu.Lock()
	defer aliasCacheMu.Unlock()
	if v, ok := aliasCache[flavor]; ok {
		return v
	}
	path := flavorAliasesPath(flavor)
	data, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintf(runner.LogWriter(), "warning: flavor alias table %s not found: %v (using identity map)\n", path, err)
		aliasCache[flavor] = nil
		return nil
	}
	var f aliasTableFile
	if err := toml.Unmarshal(data, &f); err != nil {
		fmt.Fprintf(runner.LogWriter(), "warning: failed to parse %s: %v (using identity map)\n", path, err)
		aliasCache[flavor] = nil
		return nil
	}
	aliasCache[flavor] = f.Aliases
	return f.Aliases
}

// ResolveBuildDeps rewrites build_deps entries through the flavor's
// alias table. Entries with no alias pass through unchanged. flavor =
// "" is treated as "arch" to keep existing call sites that don't yet
// pass a flavor working.
func ResolveBuildDeps(deps []string, flavor string) []string {
	if len(deps) == 0 {
		return deps
	}
	if flavor == "" {
		flavor = "arch"
	}
	table := loadFlavorAliases(flavor)
	if len(table) == 0 {
		// Identity map (or missing file): nothing to do. Return a
		// fresh slice so callers can mutate without aliasing the
		// argument.
		out := make([]string, len(deps))
		copy(out, deps)
		return out
	}
	out := make([]string, 0, len(deps))
	for _, d := range deps {
		if alias, ok := table[d]; ok && alias != "" {
			// Multi-target: an alias value can be a
			// whitespace-separated list of target-distro package
			// names. Debian splits e.g. util-linux's headers across
			// libblkid-dev / libmount-dev / libuuid1-dev; the alias
			// table encodes that as a single string and the
			// resolver expands it here.
			for _, t := range strings.Fields(alias) {
				out = append(out, t)
			}
			continue
		}
		out = append(out, d)
	}
	return out
}
