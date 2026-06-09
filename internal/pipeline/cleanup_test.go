package pipeline

import (
	"path/filepath"
	"testing"
)

func TestNewCleanupRetainsWorkDir(t *testing.T) {
	c := NewCleanup("/work/peacock")
	if c.workDir != "/work/peacock" {
		t.Fatalf("workDir = %q, want %q", c.workDir, "/work/peacock")
	}
	if c.loopDev != "" || c.installDir != "" || c.bootDir != "" || c.imageChroot != "" {
		t.Fatalf("NewCleanup left non-workDir fields set: %+v", c)
	}
}

// TestCleanupRunEmptyState pins that a Cleanup with no acquired
// resources is a no-op: every teardown step is gated on its field
// being non-empty, so nothing shells out and Run is repeat-safe.
func TestCleanupRunEmptyState(t *testing.T) {
	cases := []struct {
		name string
		c    *Cleanup
	}{
		{"zero value", &Cleanup{}},
		{"empty workdir", NewCleanup("")},
		{"workdir only", NewCleanup(t.TempDir())},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			tc.c.Run()
			tc.c.Run() // safe to call multiple times
		})
	}
}

func TestUnderWorkDir(t *testing.T) {
	sep := string(filepath.Separator)
	cases := []struct {
		name    string
		workDir string
		path    string
		want    bool
	}{
		{"nested path", "/work/peacock", "/work/peacock/rootfs/boot", true},
		{"direct child", "/work/peacock", "/work/peacock/install", true},
		{"workdir itself is not under", "/work/peacock", "/work/peacock", false},
		{"sibling with shared prefix", "/work/peacock", "/work/peacock2/boot", false},
		{"outside entirely", "/work/peacock", "/etc", false},
		{"root path", "/work/peacock", "/", false},
		{"dotdot escape is cleaned then rejected", "/work/peacock", "/work/peacock/../../etc", false},
		{"dotdot staying inside is cleaned then accepted", "/work/peacock", "/work/peacock/a/../b", true},
		{"trailing slash on workdir cleaned", "/work/peacock" + sep, "/work/peacock/boot", true},
		{"empty workdir never matches absolute path", "", "/etc", false},
		{"empty workdir never matches relative path", "", "etc", false},
		{"relative workdir matches its children", "work", "work/boot", true},
		{"relative workdir rejects others", "work", "other/boot", false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := underWorkDir(tc.workDir, tc.path); got != tc.want {
				t.Fatalf("underWorkDir(%q, %q) = %v, want %v", tc.workDir, tc.path, got, tc.want)
			}
		})
	}
}
