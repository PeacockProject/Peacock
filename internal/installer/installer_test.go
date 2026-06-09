package installer

import (
	"strings"
	"testing"
)

func TestConfigValidate(t *testing.T) {
	good := Config{
		TargetDiskNode: "/dev/sda",
		User:           UserSpec{Username: "peacock", Password: "hunter2"},
		Hostname:       "peacock-box",
		Locale:         "en_US.UTF-8",
		Keymap:         "us",
		Timezone:       "America/Los_Angeles",
	}

	cases := []struct {
		name    string
		mutate  func(*Config)
		wantErr string
	}{
		{name: "happy path", mutate: func(c *Config) {}},
		{
			name:    "missing target disk",
			mutate:  func(c *Config) { c.TargetDiskNode = "" },
			wantErr: "TargetDiskNode is required",
		},
		{
			name:    "non-/dev path",
			mutate:  func(c *Config) { c.TargetDiskNode = "sda" },
			wantErr: "must be a /dev/ path",
		},
		{
			name:    "manual partmode unsupported",
			mutate:  func(c *Config) { c.PartMode = "manual" },
			wantErr: "PartMode=manual is not supported",
		},
		{
			name:    "garbage partmode",
			mutate:  func(c *Config) { c.PartMode = "nuke-from-orbit" },
			wantErr: "is not valid",
		},
		{
			name:    "missing username",
			mutate:  func(c *Config) { c.User.Username = "" },
			wantErr: "User.Username is required",
		},
		{
			name:    "uppercase username rejected",
			mutate:  func(c *Config) { c.User.Username = "Peacock" },
			wantErr: "not a valid unix username",
		},
		{
			name:    "username starting with digit rejected",
			mutate:  func(c *Config) { c.User.Username = "1peacock" },
			wantErr: "not a valid unix username",
		},
		{
			name:    "missing password",
			mutate:  func(c *Config) { c.User.Password = "" },
			wantErr: "User.Password is required",
		},
		{
			name:    "missing hostname",
			mutate:  func(c *Config) { c.Hostname = "" },
			wantErr: "Hostname is required",
		},
		{
			name:    "hostname with underscore rejected",
			mutate:  func(c *Config) { c.Hostname = "peacock_box" },
			wantErr: "not a valid hostname",
		},
		{
			name:    "missing locale",
			mutate:  func(c *Config) { c.Locale = "" },
			wantErr: "Locale is required",
		},
		{
			name:    "garbage bootloader mode",
			mutate:  func(c *Config) { c.BootloaderMode = "lilo" },
			wantErr: "BootloaderMode \"lilo\" is not valid",
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := good
			tc.mutate(&cfg)
			err := cfg.Validate()
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("expected no error, got %v", err)
				}
				return
			}
			if err == nil {
				t.Fatalf("expected error containing %q, got nil", tc.wantErr)
			}
			if !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("expected error containing %q, got %q", tc.wantErr, err.Error())
			}
		})
	}
}

func TestConfigValidateAppliesDefaults(t *testing.T) {
	cfg := Config{
		TargetDiskNode: "/dev/sda",
		User:           UserSpec{Username: "peacock", Password: "hunter2"},
		Hostname:       "peacock-box",
		Locale:         "en_US.UTF-8",
		Keymap:         "us",
		Timezone:       "America/Los_Angeles",
	}
	if err := cfg.Validate(); err != nil {
		t.Fatalf("Validate: %v", err)
	}
	if cfg.SourceRoot != "/run/live" {
		t.Errorf("SourceRoot default = %q, want /run/live", cfg.SourceRoot)
	}
	if cfg.PartMode != "erase" {
		t.Errorf("PartMode default = %q, want erase", cfg.PartMode)
	}
	if cfg.BootloaderMode != "" {
		t.Errorf("BootloaderMode default should stay empty (filled by RunInstall), got %q", cfg.BootloaderMode)
	}
}

func TestDefaultLayout(t *testing.T) {
	cases := []struct {
		mode    string
		wantFS  string
		wantGPT bool
	}{
		{mode: "grub", wantFS: "vfat", wantGPT: true},
		{mode: "extlinux", wantFS: "ext2", wantGPT: false},
		{mode: "", wantFS: "vfat", wantGPT: true},
		{mode: "anything-else", wantFS: "vfat", wantGPT: true},
	}
	for _, tc := range cases {
		t.Run(tc.mode, func(t *testing.T) {
			got := DefaultLayout(tc.mode)
			if got.BootFS != tc.wantFS {
				t.Errorf("BootFS = %q, want %q", got.BootFS, tc.wantFS)
			}
			if got.UseGPT != tc.wantGPT {
				t.Errorf("UseGPT = %v, want %v", got.UseGPT, tc.wantGPT)
			}
			if got.RootEnd != "100%" {
				t.Errorf("RootEnd = %q, want 100%%", got.RootEnd)
			}
			if got.BootMB <= 0 {
				t.Errorf("BootMB = %d, want > 0", got.BootMB)
			}
		})
	}
}

func TestPartitionNode(t *testing.T) {
	cases := []struct {
		disk string
		idx  int
		want string
	}{
		{"/dev/sda", 1, "/dev/sda1"},
		{"/dev/sda", 2, "/dev/sda2"},
		{"/dev/nvme0n1", 1, "/dev/nvme0n1p1"},
		{"/dev/nvme0n1", 2, "/dev/nvme0n1p2"},
		{"/dev/mmcblk0", 1, "/dev/mmcblk0p1"},
		{"/dev/loop0", 1, "/dev/loop0p1"},
	}
	for _, tc := range cases {
		got := PartitionNode(tc.disk, tc.idx)
		if got != tc.want {
			t.Errorf("PartitionNode(%q,%d) = %q, want %q", tc.disk, tc.idx, got, tc.want)
		}
	}
}

func TestPartitionToDisk(t *testing.T) {
	cases := map[string]string{
		"/dev/sda1":        "/dev/sda",
		"/dev/sdb12":       "/dev/sdb",
		"/dev/nvme0n1p1":   "/dev/nvme0n1",
		"/dev/nvme0n1p12":  "/dev/nvme0n1",
		"/dev/mmcblk0p1":   "/dev/mmcblk0",
		"/dev/sda":         "/dev/sda",
		"/not/a/dev/sda1":  "",
	}
	for in, want := range cases {
		got := partitionToDisk(in)
		if got != want {
			t.Errorf("partitionToDisk(%q) = %q, want %q", in, got, want)
		}
	}
}

// lsblkFixture mirrors `lsblk -J -b -o NAME,TYPE,SIZE,MODEL,MOUNTPOINT,RM,VENDOR,FSTYPE`
// on a typical x86 host with one NVMe internal + one USB stick. zram is
// also present and must be filtered.
const lsblkFixture = `{
   "blockdevices": [
      {
         "name": "zram0", "type": "disk", "size": 16526606336,
         "model": null, "mountpoint": "[SWAP]", "rm": false, "vendor": null, "fstype": null
      },
      {
         "name": "nvme0n1", "type": "disk", "size": 512110190592,
         "model": "Micron MTFDKCD512QFM", "mountpoint": null, "rm": false,
         "vendor": null, "fstype": null,
         "children": [
            {
               "name": "nvme0n1p1", "type": "part", "size": 1073741824,
               "model": null, "mountpoint": "/boot", "rm": false,
               "vendor": null, "fstype": "ext4"
            },
            {
               "name": "nvme0n1p2", "type": "part", "size": 17179869184,
               "model": null, "mountpoint": "[SWAP]", "rm": false,
               "vendor": null, "fstype": "swap"
            }
         ]
      },
      {
         "name": "sda", "type": "disk", "size": 16000000000,
         "model": "USB DISK", "mountpoint": null, "rm": true,
         "vendor": "SanDisk", "fstype": null,
         "children": [
            {
               "name": "sda1", "type": "part", "size": 16000000000,
               "model": null, "mountpoint": "/run/live", "rm": true,
               "vendor": null, "fstype": "vfat"
            }
         ]
      }
   ]
}`

func TestParseLsblk(t *testing.T) {
	disks, err := parseLsblk([]byte(lsblkFixture), "")
	if err != nil {
		t.Fatalf("parseLsblk: %v", err)
	}
	if len(disks) != 2 {
		t.Fatalf("len(disks) = %d, want 2 (zram filtered)", len(disks))
	}

	byNode := map[string]DiskInfo{}
	for _, d := range disks {
		byNode[d.Node] = d
	}

	nvme, ok := byNode["/dev/nvme0n1"]
	if !ok {
		t.Fatalf("missing /dev/nvme0n1 in %+v", byNode)
	}
	if nvme.Removable {
		t.Errorf("nvme0n1 should not be removable")
	}
	if !strings.Contains(nvme.Name, "Micron") {
		t.Errorf("nvme0n1 name = %q, want vendor/model containing Micron", nvme.Name)
	}
	if nvme.SizeBytes != 512110190592 {
		t.Errorf("nvme0n1 SizeBytes = %d, want 512110190592", nvme.SizeBytes)
	}
	if len(nvme.Children) != 2 {
		t.Errorf("nvme0n1 children = %d, want 2", len(nvme.Children))
	}

	sda, ok := byNode["/dev/sda"]
	if !ok {
		t.Fatalf("missing /dev/sda in %+v", byNode)
	}
	if !sda.Removable {
		t.Errorf("sda should be removable")
	}
	if !strings.Contains(sda.Name, "SanDisk") {
		t.Errorf("sda name = %q, want vendor/model containing SanDisk", sda.Name)
	}
}

func TestParseLsblkExcludesLiveMedium(t *testing.T) {
	disks, err := parseLsblk([]byte(lsblkFixture), "/dev/sda")
	if err != nil {
		t.Fatalf("parseLsblk: %v", err)
	}
	for _, d := range disks {
		if d.Node == "/dev/sda" {
			t.Fatalf("live medium /dev/sda should have been excluded; got %+v", d)
		}
	}
	if len(disks) != 1 {
		t.Fatalf("len(disks) = %d, want 1 (zram + sda filtered)", len(disks))
	}
}

func TestParseLsblkRejectsBadJSON(t *testing.T) {
	_, err := parseLsblk([]byte(`{not json`), "")
	if err == nil {
		t.Fatal("expected error on garbage JSON")
	}
}

func TestParseRsyncProgress(t *testing.T) {
	cases := []struct {
		line string
		want int
		ok   bool
	}{
		{line: "    832,512  62%   12.34MB/s    0:00:15 (xfr#42, to-chk=12/96)", want: 62, ok: true},
		{line: "        1024   0%    1.00kB/s    0:00:00", want: 0, ok: true},
		{line: " 1234567890  100%   500.00MB/s    0:00:00 (xfr#1, to-chk=0/1)", want: 100, ok: true},
		{line: "sending incremental file list", want: 0, ok: false},
		{line: "./", want: 0, ok: false},
		{line: "", want: 0, ok: false},
	}
	for _, tc := range cases {
		got, ok := parseRsyncProgress(tc.line)
		if ok != tc.ok {
			t.Errorf("parseRsyncProgress(%q) ok = %v, want %v", tc.line, ok, tc.ok)
		}
		if got != tc.want {
			t.Errorf("parseRsyncProgress(%q) = %d, want %d", tc.line, got, tc.want)
		}
	}
}

func TestHumanSize(t *testing.T) {
	cases := []struct {
		in   uint64
		want string
	}{
		{0, "0 B"},
		{512, "512 B"},
		{1024, "1 KB"}, // exactly 1K → integer formatted
		{1536, "1.5 KB"},
		{16 * 1024 * 1024 * 1024, "16 GB"},
		{500 * 1024 * 1024 * 1024, "500 GB"},
	}
	for _, tc := range cases {
		got := humanSize(tc.in)
		if got != tc.want {
			t.Errorf("humanSize(%d) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestRmToBool(t *testing.T) {
	cases := []struct {
		in   interface{}
		want bool
	}{
		{true, true},
		{false, false},
		{float64(1), true},
		{float64(0), false},
		{"1", true},
		{"0", false},
		{"true", true},
		{nil, false},
	}
	for _, tc := range cases {
		got := rmToBool(tc.in)
		if got != tc.want {
			t.Errorf("rmToBool(%v) = %v, want %v", tc.in, got, tc.want)
		}
	}
}
