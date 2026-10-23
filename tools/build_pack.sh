#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
#
# Build the blur-tr translation payload: encode the committed
# per-bundle JSONs under data/translated-tr-in-en/ into OLTX binaries
# under data/oltx-tr/, then build the ASI shim.
#
# Optional:
#   $TRANSLATION_V2   if set, re-run tools/transform_translations.py
#                     from this directory of 6-slot JSONs first.  Useful
#                     when the Turkish source has changed; otherwise the
#                     committed data/translated-tr-in-en/ is used as-is.
#
# Outputs:
#   data/oltx-tr/<bundle>.oltx                 (one per JSON)
#   src/asi/blur_tr.asi                        (built with `make`)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DATA="$ROOT/data"
OUT="$DATA/oltx-tr"
JSON_DIR="$DATA/translated-tr-in-en"

say() { printf '  %s\n' "$*"; }

# 1. Re-run the Turkish transform if the user pointed at a fresh source tree.
if [ -n "${TRANSLATION_V2:-}" ]; then
  if [ ! -d "$TRANSLATION_V2" ]; then
    echo "TRANSLATION_V2 is set but not a directory: $TRANSLATION_V2" >&2
    exit 1
  fi
  say "transforming source JSONs from $TRANSLATION_V2"
  python3 "$HERE/transform_translations.py" "$TRANSLATION_V2" "$JSON_DIR"
fi

[ -d "$JSON_DIR" ] || { echo "translated JSON dir missing: $JSON_DIR" >&2; exit 1; }
mkdir -p "$OUT"

# 2. Encode every JSON to an OLTX binary.
say "encoding OLTX bundles -> $OUT"
for j in "$JSON_DIR"/*.json; do
  name="$(basename "$j" .json)"
  python3 "$HERE/oltx.py" encode "$j" "$OUT/${name}.oltx"
  say "  wrote  $OUT/${name}.oltx"
done

# 3. Build the ASI shim.
if [ -f "$ROOT/src/asi/Makefile" ]; then
  say "building src/asi/blur_tr.asi"
  make -C "$ROOT/src/asi"
fi

echo
say "pack built."
du -sh "$OUT"
