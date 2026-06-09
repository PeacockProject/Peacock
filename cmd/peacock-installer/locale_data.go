package main

// Locale + region + keyboard reference lists for the install wizard.
//
// Mirror of the mock JSX's hard-coded arrays in InstallFlow.jsx so the
// wizard's Location / Keyboard / Welcome steps render identically when
// the frontend asks the backend instead of inlining.
//
// We bind ListLocaleOptions() rather than three separate functions so
// the frontend gets one round-trip per wizard load. The shape stays a
// flat struct (rather than a map) so Wails' generated TS / JS bindings
// expose camel-cased field accessors.

// Region is one timezone option shown on the Location step.
type Region struct {
	TZ     string `json:"tz"`
	Offset string `json:"off"`
}

// Keyboard is one entry on the Keyboard step.
type Keyboard struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Mode string `json:"m"` // "QWERTY" | "QWERTZ" | "AZERTY"
}

// LocaleOptions is the bundle the install wizard requests at startup.
type LocaleOptions struct {
	Languages []string   `json:"languages"`
	Regions   []Region   `json:"regions"`
	Keyboards []Keyboard `json:"keyboards"`
}

// Languages mirrors LANGS in the mock InstallFlow.jsx. Order matters —
// the wizard renders them in array order.
var Languages = []string{
	"English",
	"Français",
	"Deutsch",
	"Español",
	"日本語",
	"Nederlands",
	"Português",
	"Italiano",
	"Polski",
	"简体中文",
}

// Regions mirrors REGIONS in the mock. The wizard maps a Region to
// Config.Timezone via the TZ field.
var Regions = []Region{
	{TZ: "Europe/Amsterdam", Offset: "UTC+1"},
	{TZ: "Europe/London", Offset: "UTC+0"},
	{TZ: "America/New_York", Offset: "UTC−5"},
	{TZ: "America/Los_Angeles", Offset: "UTC−8"},
	{TZ: "Asia/Tokyo", Offset: "UTC+9"},
	{TZ: "Australia/Sydney", Offset: "UTC+11"},
}

// Keyboards mirrors KEYBOARDS in the mock. The wizard maps a Keyboard
// to Config.Keymap via the ID field.
var Keyboards = []Keyboard{
	{ID: "us", Name: "English (US)", Mode: "QWERTY"},
	{ID: "uk", Name: "English (UK)", Mode: "QWERTY"},
	{ID: "de", Name: "German", Mode: "QWERTZ"},
	{ID: "fr", Name: "French", Mode: "AZERTY"},
	{ID: "nl", Name: "Dutch", Mode: "QWERTY"},
	{ID: "es", Name: "Spanish", Mode: "QWERTY"},
}

// ListLocaleOptions is the bound Wails method exposing all three
// arrays in a single round-trip.
func (a *App) ListLocaleOptions() LocaleOptions {
	return LocaleOptions{
		Languages: Languages,
		Regions:   Regions,
		Keyboards: Keyboards,
	}
}
