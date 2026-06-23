package pipeline

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// buildFeather writes a tiny .feather (gzip tar) under dir from the given
// members (archive-relative path -> content) and returns its path. Members are
// archived under the top-level "files/" tree, matching a real .feather.
func buildFeather(t *testing.T, dir, name string, members map[string]string) string {
	t.Helper()
	stage := filepath.Join(dir, "stage-"+name)
	for rel, content := range members {
		p := filepath.Join(stage, rel)
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	arch := filepath.Join(dir, name+".feather")
	cmd := exec.Command("tar", "-czf", arch, "-C", stage, "files")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("tar build: %v: %s", err, out)
	}
	return arch
}

// extractKernelFromPackage must stage zImage AND a real DTB; a feather with no
// .dtb would otherwise ship extlinux with no `fdt` and brick boot ("No DTB
// configured"). This locks the fresh-extract validation.
func TestExtractKernelFromPackage(t *testing.T) {
	silenceRunnerLog(t)

	t.Run("zImage + dtb extracts", func(t *testing.T) {
		dir := t.TempDir()
		fea := buildFeather(t, dir, "ok", map[string]string{
			"files/boot/zImage":             "ZIMG",
			"files/boot/dtbs/qcom/board.dtb": "DTB",
			"files/boot/config":             "CONFIG",
		})
		dest, err := extractKernelFromPackage(fea, filepath.Join(dir, "work"))
		if err != nil {
			t.Fatalf("extract failed: %v", err)
		}
		if _, err := os.Stat(filepath.Join(dest, "zImage")); err != nil {
			t.Errorf("zImage not staged: %v", err)
		}
		if !dirHasDTB(filepath.Join(dest, "dtbs")) {
			t.Error("dtbs/ staged without a .dtb")
		}
	})

	t.Run("dtbs/ present but no .dtb is rejected (the brick guard)", func(t *testing.T) {
		dir := t.TempDir()
		fea := buildFeather(t, dir, "nodtb", map[string]string{
			"files/boot/zImage":         "ZIMG",
			"files/boot/dtbs/README.txt": "no device tree here",
		})
		_, err := extractKernelFromPackage(fea, filepath.Join(dir, "work"))
		if err == nil {
			t.Fatal("expected error for a DTB-less dtbs/, got nil")
		}
		if !strings.Contains(err.Error(), "no DTB found") {
			t.Errorf("err = %v, want it to mention 'no DTB found'", err)
		}
	})

	t.Run("no dtbs member at all errors", func(t *testing.T) {
		dir := t.TempDir()
		fea := buildFeather(t, dir, "bare", map[string]string{
			"files/boot/zImage": "ZIMG",
		})
		if _, err := extractKernelFromPackage(fea, filepath.Join(dir, "work")); err == nil {
			t.Fatal("expected error when files/boot/dtbs is absent, got nil")
		}
	})
}
