#!/bin/sh
# Sets up a Wine prefix for testing.

# Fedora packages:
# wine-core.x86_64 wine-core.i686 wine-pulseaudio.x86_64 wine-pulseaudio.i686 wine-mono wine-fonts winetricks

set -eu

log() { echo "$@" >&2; }
bye() { log "$@"; exit 0; }
die() { log "$@"; exit 1; }

command -v wine >/dev/null 2>&1 || die "wine is not installed"
command -v winetricks >/dev/null 2>&1 || die "winetricks is not installed"

export WINEPREFIX=$PWD/wineprefix
export WINEARCH=win32

[ -d "$WINEPREFIX" ] && bye "wineprefix already exists: $WINEPREFIX"

log "Installing wine components"
winetricks d3dx9_43 d3dcompiler_43 arial cjkfonts

log "Done. Run the following commands before running wine:

export WINEPREFIX=$WINEPREFIX
export WINEARCH=$WINEARCH"