package builder

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"

	"github.com/pelletier/go-toml/v2"

	"peacock/internal/runner"
)

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
// alias table.
func flavorAliasesPath(flavor string) string {
	return filepath.Join("peacock-ports", "flavors", flavor, "aliases.toml")
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
			out = append(out, alias)
			continue
		}
		out = append(out, d)
	}
	return out
}
