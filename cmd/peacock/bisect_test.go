package main

import (
	"reflect"
	"testing"

	"peacock/internal/manifest"
)

// pkgWith assembles a *manifest.Package with the given local-build
// dependencies. Keeps the test fixtures readable.
func pkgWith(name string, deps ...string) *manifest.Package {
	p := &manifest.Package{}
	p.Package.Name = name
	p.Build.Dependencies = deps
	return p
}

func TestComputeBuildOrder_SingleNode(t *testing.T) {
	manifests := map[string]*manifest.Package{
		"a": pkgWith("a"),
	}
	got, err := computeBuildOrder("a", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	if !reflect.DeepEqual(got, []string{"a"}) {
		t.Fatalf("order = %v, want [a]", got)
	}
}

func TestComputeBuildOrder_LinearChain(t *testing.T) {
	// c depends on b, b depends on a. Expected leaves-up order:
	// [a, b, c].
	manifests := map[string]*manifest.Package{
		"a": pkgWith("a"),
		"b": pkgWith("b", "a"),
		"c": pkgWith("c", "b"),
	}
	got, err := computeBuildOrder("c", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	want := []string{"a", "b", "c"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("order = %v, want %v", got, want)
	}
}

func TestComputeBuildOrder_Diamond(t *testing.T) {
	// d depends on b + c, both depend on a. The shared dep `a` must
	// only appear once and must precede both b and c, which both
	// precede d. Deps are sorted before traversal so the order is
	// deterministic across runs.
	manifests := map[string]*manifest.Package{
		"a": pkgWith("a"),
		"b": pkgWith("b", "a"),
		"c": pkgWith("c", "a"),
		"d": pkgWith("d", "c", "b"),
	}
	got, err := computeBuildOrder("d", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	want := []string{"a", "b", "c", "d"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("order = %v, want %v", got, want)
	}
}

func TestComputeBuildOrder_SkipsUnknownDeps(t *testing.T) {
	// b's dep `system-pkg` is not in the manifests map — treated as a
	// system package and dropped silently.
	manifests := map[string]*manifest.Package{
		"a": pkgWith("a"),
		"b": pkgWith("b", "a", "system-pkg"),
	}
	got, err := computeBuildOrder("b", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	want := []string{"a", "b"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("order = %v, want %v", got, want)
	}
}

func TestComputeBuildOrder_CycleDetected(t *testing.T) {
	manifests := map[string]*manifest.Package{
		"a": pkgWith("a", "b"),
		"b": pkgWith("b", "a"),
	}
	_, err := computeBuildOrder("a", manifests)
	if err == nil {
		t.Fatalf("computeBuildOrder cycle: err = nil, want cycle error")
	}
}

func TestComputeBuildOrder_DepWithVersionSpecStripped(t *testing.T) {
	// "a>=1.2.3" should normalize to "a" via normalizeDepName.
	a := pkgWith("a")
	b := pkgWith("b", "a>=1.2.3")
	manifests := map[string]*manifest.Package{
		"a": a,
		"b": b,
	}
	got, err := computeBuildOrder("b", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	want := []string{"a", "b"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("order = %v, want %v", got, want)
	}
}

func TestComputeBuildOrder_PackageDependsAlsoWalked(t *testing.T) {
	// Both Build.Dependencies and Package.Depends contribute to the
	// graph. b lists `a` only under Package.Depends to confirm that
	// branch is walked.
	a := pkgWith("a")
	b := &manifest.Package{}
	b.Package.Name = "b"
	b.Package.Depends = []string{"a"}
	manifests := map[string]*manifest.Package{"a": a, "b": b}
	got, err := computeBuildOrder("b", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	want := []string{"a", "b"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("order = %v, want %v", got, want)
	}
}

func TestComputeBuildOrder_SelfDepIgnored(t *testing.T) {
	// A manifest that names itself in deps must not trip the cycle
	// detector. normalizeDepName + the equal-name guard drop it
	// silently.
	a := pkgWith("a", "a")
	manifests := map[string]*manifest.Package{"a": a}
	got, err := computeBuildOrder("a", manifests)
	if err != nil {
		t.Fatalf("computeBuildOrder: %v", err)
	}
	if !reflect.DeepEqual(got, []string{"a"}) {
		t.Fatalf("order = %v, want [a]", got)
	}
}

func TestComputeBuildOrder_UnknownRoot(t *testing.T) {
	// Unknown root returns empty order (no error) — computeBuildOrder
	// treats absent manifests as already-satisfied. The driver
	// (runBisect) is responsible for surfacing "local package not
	// found" via loadLocalManifests.
	got, err := computeBuildOrder("nope", map[string]*manifest.Package{})
	if err != nil {
		t.Fatalf("computeBuildOrder unknown root: %v", err)
	}
	if len(got) != 0 {
		t.Fatalf("order = %v, want []", got)
	}
}

func TestNormalizeBisectDepName(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"a", "a"},
		{"a>=1.0", "a"},
		{"a<2.0", "a"},
		{"a=1.2.3", "a"},
		{"a 1.2.3", "a"},
		{"  spaced  ", "spaced"},
		{"", ""},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.in, func(t *testing.T) {
			got := normalizeDepName(tc.in)
			if got != tc.want {
				t.Fatalf("normalizeDepName(%q) = %q, want %q", tc.in, got, tc.want)
			}
		})
	}
}
