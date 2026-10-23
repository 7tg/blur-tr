#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
#
# Install the Turkish language pack into a Blur (2010 PC) game folder.
#
# Usage: tools/install.sh <path-to-Blur-game-folder>
#
# What it does:
#   - verifies the target looks like a Blur install (Blur.exe + cache/ present)
#   - installs src/asi/blur_tr.asi into <game>/scripts/ (built via
#     `make -C src/asi` if the .asi is not already present)
#   - stages data/oltx-tr/*.oltx into <game>/scripts/blur-tr-oltx/, which the
#     ASI reads at startup and swaps into the engine's OLTX parser in memory
#   - backs up any pre-existing scripts/blur_tr.asi one time, into
#     <game>/.blur-tr-backup/
#   - writes a manifest at <game>/.blur-tr-backup/manifest.txt
#
# No Activision assets are shipped; no files under gamedata/ are touched; no
# language marker (english.txt / polish.txt) is created or removed.
#
# Uninstall: tools/uninstall.sh <path-to-Blur-game-folder>.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DATA="$ROOT/data"
ASI_SRC="$ROOT/src/asi/blur_tr.asi"

if [ $# -lt 1 ]; then
  cat <<USAGE >&2
usage: $0 <path-to-Blur-game-folder>

Example (Linux):
  $0 "/home/you/Games/Blur"

Example (Wine prefix game dir):
  $0 "/home/you/.wine/drive_c/Program Files (x86)/Activision/Blur"
USAGE
  exit 2
fi

GAME="$1"
[ -d "$GAME" ] || { echo "not a directory: $GAME" >&2; exit 1; }
[ -f "$GAME/Blur.exe" ] || { echo "Blur.exe not found in $GAME" >&2; exit 1; }
[ -d "$GAME/cache" ] || { echo "cache/ not found in $GAME (not a Blur install)" >&2; exit 1; }
[ -d "$DATA/oltx-tr" ] || { echo "pack data missing: $DATA/oltx-tr not found" >&2; exit 1; }

BACKUP="$GAME/.blur-tr-backup"
mkdir -p "$BACKUP"
MANIFEST="$BACKUP/manifest.txt"
: > "$MANIFEST"

say() { printf '  %s\n' "$*"; }

# 1. Build the ASI on demand if it hasn't been built already.
if [ ! -f "$ASI_SRC" ]; then
  if [ -f "$ROOT/src/asi/Makefile" ]; then
    say "building    src/asi/blur_tr.asi"
    make -C "$ROOT/src/asi" >/dev/null
  fi
fi
[ -f "$ASI_SRC" ] || { echo "src/asi/blur_tr.asi missing and could not be built" >&2; exit 1; }

# 2. Install the ASI into scripts/, backing up any existing one first.
mkdir -p "$GAME/scripts"
if [ -f "$GAME/scripts/blur_tr.asi" ] && [ ! -f "$BACKUP/scripts/blur_tr.asi" ]; then
  mkdir -p "$BACKUP/scripts"
  cp -p "$GAME/scripts/blur_tr.asi" "$BACKUP/scripts/blur_tr.asi"
  say "backed up   scripts/blur_tr.asi (pre-existing copy)"
fi
cp -p "$ASI_SRC" "$GAME/scripts/blur_tr.asi"
printf '%s\n' "scripts/blur_tr.asi" >> "$MANIFEST"
say "installed   scripts/blur_tr.asi"

# 3. Stage the OLTX payload under scripts/blur-tr-oltx/.
OLTX_TR="$DATA/oltx-tr"
if ls "$OLTX_TR"/*.oltx >/dev/null 2>&1; then
  mkdir -p "$GAME/scripts/blur-tr-oltx"
  # Fresh install: wipe any stale .oltx we had placed previously so the
  # directory reflects the current pack exactly (only when we own it).
  if [ -f "$GAME/scripts/blur-tr-oltx/.blur-tr-managed" ]; then
    rm -f "$GAME/scripts/blur-tr-oltx/"*.oltx
  fi
  cp -p "$OLTX_TR"/*.oltx "$GAME/scripts/blur-tr-oltx/"
  : > "$GAME/scripts/blur-tr-oltx/.blur-tr-managed"
  count=$(ls -1 "$OLTX_TR"/*.oltx | wc -l)
  printf '%s\n' "scripts/blur-tr-oltx" >> "$MANIFEST"
  say "installed   scripts/blur-tr-oltx/ ($count .oltx files)"
else
  echo "WARNING: no .oltx files in $OLTX_TR; the ASI will log 'no replacement OLTX files found'." >&2
fi

echo
say "install complete."
say "to revert:  $HERE/uninstall.sh \"$GAME\""
