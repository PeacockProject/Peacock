# peacock-builder distribution targets.
#
# `make appimage` builds Linux + bundles AppImage.
# `make dmg`     builds macOS universal app + creates .dmg.
# `make exe`     builds Windows amd64 single .exe (no installer wrapper).
# `make all`     builds all three.
# `make clean`   removes build/, dist/.
# `make help`    lists targets + prereq install hints.

WAILS ?= $(HOME)/go/bin/wails
APPIMAGETOOL ?= appimagetool
CREATEDMG ?= create-dmg

VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
LDFLAGS := -X main.version=$(VERSION)

.PHONY: all appimage dmg exe clean help

all: appimage dmg exe

appimage:
	$(WAILS) build -platform linux/amd64 -ldflags "$(LDFLAGS)"
	@echo "-> wrapping into AppImage..."
	rm -rf dist/AppDir
	mkdir -p dist/AppDir/usr/bin
	cp build/bin/peacock-builder dist/AppDir/usr/bin/
	cp build/appicon.png dist/AppDir/peacock-builder.png || true
	printf '%s\n' \
		'[Desktop Entry]' \
		'Name=PeacockBuilder' \
		'Exec=peacock-builder' \
		'Icon=peacock-builder' \
		'Type=Application' \
		'Categories=Development;' \
		> dist/AppDir/peacock-builder.desktop
	printf '%s\n' \
		'#!/bin/sh' \
		'HERE="$$(dirname "$$(readlink -f "$$0")")"' \
		'exec "$$HERE/usr/bin/peacock-builder" "$$@"' \
		> dist/AppDir/AppRun
	chmod +x dist/AppDir/AppRun
	$(APPIMAGETOOL) dist/AppDir dist/peacock-builder-$(VERSION)-x86_64.AppImage

dmg:
	$(WAILS) build -platform darwin/universal -ldflags "$(LDFLAGS)"
	@echo "-> creating .dmg..."
	mkdir -p dist
	$(CREATEDMG) \
		--volname "PeacockBuilder $(VERSION)" \
		--window-size 600 400 \
		--icon "PeacockBuilder.app" 175 200 \
		--hide-extension "PeacockBuilder.app" \
		--app-drop-link 425 200 \
		dist/peacock-builder-$(VERSION).dmg \
		build/bin/PeacockBuilder.app

exe:
	$(WAILS) build -platform windows/amd64 -ldflags "$(LDFLAGS)" -o peacock-builder-$(VERSION).exe
	mkdir -p dist
	mv build/bin/peacock-builder-$(VERSION).exe dist/

clean:
	rm -rf build/ dist/

help:
	@echo "PeacockBuilder distribution Makefile"
	@echo
	@echo "Targets:"
	@echo "  appimage  Build the Linux AppImage (requires wails, appimagetool)"
	@echo "  dmg       Build the macOS .dmg (requires wails, create-dmg)"
	@echo "  exe       Build the Windows .exe (requires wails + windows cross toolchain)"
	@echo "  all       Build all three"
	@echo "  clean     Remove build/ and dist/"
	@echo
	@echo "Prereqs:"
	@echo "  wails       go install github.com/wailsapp/wails/v2/cmd/wails@latest"
	@echo "  appimagetool https://github.com/AppImage/AppImageKit/releases"
	@echo "  create-dmg  brew install create-dmg  (macOS only)"
