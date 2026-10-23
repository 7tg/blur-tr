#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
#
# Revert what tools/install.sh did to a Blur game folder.
#
# Usage: tools/uninstall.sh <path-to-Blur-game-folder>

set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <path-to-Blur-game-folder>" >&2
  exit 2
fi

GAME="$1"
[ -d "$GAME" ] || { echo "not a directory: $GAME" >&2; exit 1; }
BACKUP="$GAME/.blur-tr-backup"
MANIFEST="$BACKUP/manifest.txt"
[ -f "$MANIFEST" ] || { echo "nothing to undo (no manifest at $MANIFEST)" >&2; exit 1; }

say() { printf '  %s\n' "$*"; }

while IFS= read -r rel; do
  [ -z "$rel" ] && continue
  if [ -f "$BACKUP/$rel" ]; then
    cp -p "$BACKUP/$rel" "$GAME/$rel"
    say "restored    $rel"
  elif [ -d "$GAME/$rel" ]; then
    rm -rf "$GAME/$rel"
    say "removed     $rel/"
  else
    rm -f "$GAME/$rel"
    say "removed     $rel"
  fi
done < "$MANIFEST"

rm -rf "$BACKUP"
say "done. the game is back to its shipped state."
