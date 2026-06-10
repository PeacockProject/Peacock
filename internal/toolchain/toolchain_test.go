package toolchain

import (
	"os"
	"path/filepath"
	"testing"
)

// writeFixture drops a toolchains.toml into a temp dir and points Root at
// it for the duration of the test.
func writeFixture(t *testing.T) {
	t.Helper()
	dir := t.TempDir()
	const toml = `
[triples]
aarch64 = "aarch64-linux-gnu"
armv7h  = "arm-linux-gnueabihf"

[debarch]
aarch64 = "arm64"
armv7h  = "armhf"

[capabilities.c-toolchain.native.arch]
packages = ["base-devel"]
[capabilities.c-toolchain.native.debian]
packages = ["build-essential"]

[capabilities.c-toolchain.cross.arch]
packages = ["{triple}-gcc", "{triple}-binutils"]
[capabilities.c-toolchain.cross.debian]
packages = ["crossbuild-essential-{debarch}"]
[capabilities.c-toolchain.cross.alpine]
unsupported = "no linux-gnu cross on Alpine"
`
	if err := os.WriteFile(filepath.Join(dir, "toolchains.toml"), []byte(toml), 0644); err != nil {
		t.Fatal(err)
	}
	old := Root
	Root = dir
	t.Cleanup(func() { Root = old })
}

func eq(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestResolveCrossArch(t *testing.T) {
	writeFixture(t)
	res, err := Resolve([]string{"c-toolchain"}, "aarch64", "", "arch", true)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"aarch64-linux-gnu-gcc", "aarch64-linux-gnu-binutils"}
	if !eq(res.Packages, want) {
		t.Fatalf("packages = %v, want %v", res.Packages, want)
	}
	if res.CrossCompile != "aarch64-linux-gnu-" {
		t.Fatalf("CrossCompile = %q, want aarch64-linux-gnu-", res.CrossCompile)
	}
}

func TestResolveCrossDebianUsesMeta(t *testing.T) {
	writeFixture(t)
	res, err := Resolve([]string{"c-toolchain"}, "aarch64", "", "debian", true)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"crossbuild-essential-arm64"}
	if !eq(res.Packages, want) {
		t.Fatalf("packages = %v, want %v", res.Packages, want)
	}
}

func TestResolveNative(t *testing.T) {
	writeFixture(t)
	res, err := Resolve([]string{"c-toolchain"}, "aarch64", "", "arch", false)
	if err != nil {
		t.Fatal(err)
	}
	if !eq(res.Packages, []string{"base-devel"}) {
		t.Fatalf("packages = %v, want [base-devel]", res.Packages)
	}
	if res.CrossCompile != "" {
		t.Fatalf("native CrossCompile = %q, want empty", res.CrossCompile)
	}
}

func TestResolveTripleOverride(t *testing.T) {
	writeFixture(t)
	res, err := Resolve([]string{"c-toolchain"}, "armv7h", "arm-eabi", "arch", true)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"arm-eabi-gcc", "arm-eabi-binutils"}
	if !eq(res.Packages, want) {
		t.Fatalf("packages = %v, want %v", res.Packages, want)
	}
	if res.CrossCompile != "arm-eabi-" {
		t.Fatalf("CrossCompile = %q, want arm-eabi-", res.CrossCompile)
	}
}

func TestResolveUnsupportedFailsFast(t *testing.T) {
	writeFixture(t)
	_, err := Resolve([]string{"c-toolchain"}, "aarch64", "", "alpine", true)
	if err == nil {
		t.Fatal("expected unsupported error for alpine cross, got nil")
	}
}

func TestResolveUnknownCapability(t *testing.T) {
	writeFixture(t)
	_, err := Resolve([]string{"nonexistent"}, "aarch64", "", "arch", true)
	if err == nil {
		t.Fatal("expected error for unknown capability, got nil")
	}
}

func TestResolveMissingTripleFailsFast(t *testing.T) {
	writeFixture(t)
	// x86_64 has no [triples] entry in the fixture; cross build needs one.
	_, err := Resolve([]string{"c-toolchain"}, "x86_64", "", "arch", true)
	if err == nil {
		t.Fatal("expected missing-triple error, got nil")
	}
}

func TestResolveNoCapabilitiesDerivesCC(t *testing.T) {
	writeFixture(t)
	res, err := Resolve(nil, "aarch64", "", "arch", true)
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Packages) != 0 {
		t.Fatalf("packages = %v, want none", res.Packages)
	}
	if res.CrossCompile != "aarch64-linux-gnu-" {
		t.Fatalf("CrossCompile = %q, want aarch64-linux-gnu-", res.CrossCompile)
	}
}
