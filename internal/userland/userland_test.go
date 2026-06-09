package userland

import (
	"reflect"
	"strings"
	"testing"
)

func contains(list []string, want string) bool {
	for _, v := range list {
		if v == want {
			return true
		}
	}
	return false
}

func hasWarning(warnings []string, substr string) bool {
	for _, w := range warnings {
		if strings.Contains(w, substr) {
			return true
		}
	}
	return false
}

func TestResolveSelectionsErrors(t *testing.T) {
	cases := []struct {
		name    string
		desktop string
		dm      string
		wantErr string
	}{
		{"unknown desktop", "enlightenment", "none", "unknown desktop"},
		{"unknown display manager", "none", "wdm", "unknown display manager"},
		{"empty desktop is not a choice", "", "none", "unknown desktop"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, _, err := ResolveSelections(tc.desktop, tc.dm, "systemd", nil)
			if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("ResolveSelections(%q, %q) error = %v, want substring %q", tc.desktop, tc.dm, err, tc.wantErr)
			}
		})
	}
}

func TestResolveSelectionsPackageSets(t *testing.T) {
	cases := []struct {
		name        string
		desktop     string
		dm          string
		initSystem  string
		extra       []string
		wantHas     []string
		wantMissing []string
		wantWarn    []string
	}{
		{
			name:        "none/none yields no packages",
			desktop:     "none",
			dm:          "none",
			initSystem:  "systemd",
			wantMissing: []string{"xorg-server", "mesa"},
		},
		{
			name:       "desktop pulls base X stack",
			desktop:    "xfce",
			dm:         "none",
			initSystem: "systemd",
			wantHas:    []string{"xorg-server", "mesa", "xfce4", "xfce4-goodies"},
		},
		{
			name:       "dm alone pulls base X stack",
			desktop:    "none",
			dm:         "lightdm",
			initSystem: "systemd",
			wantHas:    []string{"xorg-server", "lightdm", "lightdm-gtk-greeter"},
		},
		{
			name:        "lightdm on openrc adds dbus/elogind",
			desktop:     "none",
			dm:          "lightdm",
			initSystem:  "openrc",
			wantHas:     []string{"lightdm", "dbus", "dbus-openrc", "elogind", "elogind-openrc"},
			wantMissing: []string{"sddm-openrc"},
		},
		{
			name:        "lightdm on systemd skips openrc shims",
			desktop:     "none",
			dm:          "lightdm",
			initSystem:  "systemd",
			wantHas:     []string{"lightdm"},
			wantMissing: []string{"dbus-openrc", "elogind-openrc"},
		},
		{
			name:       "sddm pulls theme and both qt stacks",
			desktop:    "plasma",
			dm:         "sddm",
			initSystem: "systemd",
			wantHas: []string{
				"plasma-desktop", "sddm", "peacock-sddm-theme-peacock-phone",
				"qt5-base", "qt5-declarative", "qt6-base", "qt6-declarative",
				"qt5-virtualkeyboard", "qt6-virtualkeyboard",
			},
			wantMissing: []string{"sddm-openrc"},
			wantWarn:    []string{"needs 3D acceleration"},
		},
		{
			name:       "sddm on openrc adds init shims and warns",
			desktop:    "none",
			dm:         "sddm",
			initSystem: "openrc",
			wantHas:    []string{"sddm", "sddm-openrc", "dbus-openrc", "elogind-openrc"},
			wantWarn:   []string{"sourced from local Artix init script"},
		},
		{
			name:       "gnome on openrc warns systemd-only and 3d",
			desktop:    "gnome",
			dm:         "gdm",
			initSystem: "openrc",
			wantHas:    []string{"gnome", "gdm"},
			wantWarn: []string{
				"desktop 'gnome' is typically systemd-only",
				"display manager 'gdm' is typically systemd-only",
				"desktop 'gnome' needs 3D acceleration",
				"display manager 'gdm' needs 3D acceleration",
			},
		},
		{
			name:       "extras appended and deduped",
			desktop:    "xfce",
			dm:         "none",
			initSystem: "systemd",
			extra:      []string{"htop", "xfce4", "  ", "htop"},
			wantHas:    []string{"htop"},
		},
		{
			name:       "choice names are case and space folded",
			desktop:    "  XFCE ",
			dm:         " LightDM ",
			initSystem: "systemd",
			wantHas:    []string{"xfce4", "lightdm"},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			pkgs, warnings, err := ResolveSelections(tc.desktop, tc.dm, tc.initSystem, tc.extra)
			if err != nil {
				t.Fatalf("ResolveSelections error = %v", err)
			}
			for _, want := range tc.wantHas {
				if !contains(pkgs, want) {
					t.Errorf("packages %v missing %q", pkgs, want)
				}
			}
			for _, miss := range tc.wantMissing {
				if contains(pkgs, miss) {
					t.Errorf("packages %v unexpectedly contain %q", pkgs, miss)
				}
			}
			for _, w := range tc.wantWarn {
				if !hasWarning(warnings, w) {
					t.Errorf("warnings %v missing %q", warnings, w)
				}
			}
			// The result must be deduplicated and free of blanks.
			seen := map[string]bool{}
			for _, p := range pkgs {
				if strings.TrimSpace(p) == "" {
					t.Errorf("packages contain blank entry: %v", pkgs)
				}
				if seen[p] {
					t.Errorf("packages contain duplicate %q: %v", p, pkgs)
				}
				seen[p] = true
			}
		})
	}
}

func TestResolveSelectionsNoneNoneNoWarnings(t *testing.T) {
	pkgs, warnings, err := ResolveSelections("none", "none", "openrc", nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(pkgs) != 0 {
		t.Fatalf("packages = %v, want empty", pkgs)
	}
	if len(warnings) != 0 {
		t.Fatalf("warnings = %v, want empty", warnings)
	}
}

func TestDesktopAndDisplayManagerNames(t *testing.T) {
	wantDesktops := []string{"cinnamon", "gnome", "lxqt", "mate", "none", "plasma", "xfce"}
	if got := DesktopNames(); !reflect.DeepEqual(got, wantDesktops) {
		t.Errorf("DesktopNames() = %v, want %v", got, wantDesktops)
	}
	wantDMs := []string{"gdm", "greetd", "lightdm", "ly", "none", "sddm"}
	if got := DisplayManagerNames(); !reflect.DeepEqual(got, wantDMs) {
		t.Errorf("DisplayManagerNames() = %v, want %v", got, wantDMs)
	}
}

func TestDescribeChoicesMentionsEveryChoice(t *testing.T) {
	out := DescribeChoices()
	for _, name := range append(DesktopNames(), DisplayManagerNames()...) {
		if !strings.Contains(out, name) {
			t.Errorf("DescribeChoices() missing %q", name)
		}
	}
	if !strings.Contains(out, "needs-3d") || !strings.Contains(out, "systemd-only") {
		t.Errorf("DescribeChoices() missing flag annotations:\n%s", out)
	}
}

func TestDisplayManagerService(t *testing.T) {
	cases := []struct{ in, want string }{
		{"lightdm", "lightdm"},
		{"sddm", "sddm"},
		{"gdm", "gdm"},
		{"greetd", "greetd"},
		{"ly", "ly"},
		{" SDDM ", "sddm"}, // folded
		{"none", ""},
		{"", ""},
		{"unknown", ""},
	}
	for _, tc := range cases {
		if got := DisplayManagerService(tc.in); got != tc.want {
			t.Errorf("DisplayManagerService(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestDisplayManagerOpenRCServices(t *testing.T) {
	cases := []struct {
		name       string
		dm         string
		initSystem string
		want       []OpenRCService
	}{
		{
			name:       "sddm on openrc",
			dm:         "sddm",
			initSystem: "openrc",
			want: []OpenRCService{
				{Name: "dbus", Runlevel: "default"},
				{Name: "elogind", Runlevel: "boot"},
			},
		},
		{
			name:       "lightdm on openrc",
			dm:         "lightdm",
			initSystem: "openrc",
			want: []OpenRCService{
				{Name: "dbus", Runlevel: "default"},
				{Name: "elogind", Runlevel: "boot"},
			},
		},
		{name: "sddm on systemd", dm: "sddm", initSystem: "systemd", want: nil},
		{name: "greetd on openrc", dm: "greetd", initSystem: "openrc", want: nil},
		{name: "none on openrc", dm: "none", initSystem: "openrc", want: nil},
		{
			name:       "case folded inputs",
			dm:         " GDM ",
			initSystem: " OpenRC ",
			want: []OpenRCService{
				{Name: "dbus", Runlevel: "default"},
				{Name: "elogind", Runlevel: "boot"},
			},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := DisplayManagerOpenRCServices(tc.dm, tc.initSystem)
			if !reflect.DeepEqual(got, tc.want) {
				t.Fatalf("DisplayManagerOpenRCServices(%q, %q) = %v, want %v", tc.dm, tc.initSystem, got, tc.want)
			}
		})
	}
}
