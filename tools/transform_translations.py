#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
"""Transform a 6-language Turkish OLTX JSON tree into the shipped form.

Input (``$TRANSLATION_V2`` or first positional arg):
    A directory of per-bundle JSON dumps produced by ``tools/oltx.py decode``,
    augmented with a sixth ``pl`` slot that carries hand-translated Turkish
    (with Latin-2 diacritics intact: ``ıİğĞşŞ``).

Output:
    One JSON per input bundle under ``<dst>`` (default: this repo's
    ``data/translated-tr-in-en/``).  The output JSON is identical to the
    input, with two edits applied to its ``en`` slot only:

      1. Glossary pins ("Teşhir Salonu" family -> "Showroom" family).
      2. ASCII fold of Turkish-only diacritics (ıİğĞşŞ -> iIgGsS).

Why the ``en`` slot?  Blur's engine selects a language slot by
``_stricmp(lang_code, slot_name)`` and defaults to "en" on an EN install.
Writing Turkish into "en" means no DetectLanguage / LangId patching is
required; the engine naturally reads our strings.  The engine's "en" font
is Latin-1, which cannot render ``ıİğĞşŞ`` — hence the ASCII fold.

The ``pl`` slot (Turkish with diacritics) is left intact for reference and
in case a future build swaps to the Latin-2 ``ba.dat`` font and routes
through the Polish slot.

Other slots (``fr``/``de``/``it``/``es``/``pl``) pass through unchanged.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Iterable

# Order matters: longer phrases must be matched before their prefixes.
GLOSSARY_PINS = [
    ("Teşhir Salonunda", "Showroom'da"),
    ("Teşhir salonunda", "showroom'da"),
    ("teşhir salonunda", "showroom'da"),
    ("Teşhir Salonu",   "Showroom"),
    ("Teşhir salonu",   "Showroom"),
    ("teşhir salonu",   "showroom"),
]

FOLD_TABLE = str.maketrans({
    "ı": "i", "İ": "I",
    "ğ": "g", "Ğ": "G",
    "ş": "s", "Ş": "S",
})


def transform_string(s: str) -> str:
    for src, dst in GLOSSARY_PINS:
        if src in s:
            s = s.replace(src, dst)
    return s.translate(FOLD_TABLE)


def transform_bundle(doc: dict) -> dict:
    out = dict(doc)
    strings = {lang: list(vals) for lang, vals in doc["strings"].items()}
    if "pl" not in strings:
        raise ValueError("input JSON has no 'pl' slot — expected hand-"
                         "translated Turkish there")
    # The 'en' slot gets Turkish folded to ASCII; the 'pl' slot keeps the
    # Latin-2 original.  Both are overwritten from the Turkish source so the
    # pipeline is idempotent regardless of whatever 'en' originally held.
    strings["en"] = [transform_string(v) for v in strings["pl"]]
    out["strings"] = strings
    out["language_codes"] = list(doc["language_codes"])
    return out


def iter_json(src: pathlib.Path) -> Iterable[pathlib.Path]:
    return sorted(p for p in src.glob("*.json") if p.is_file())


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", nargs="?", type=pathlib.Path,
                    help="input dir of per-bundle JSON files (6 slots each, "
                         "with Turkish in the 'pl' slot). Defaults to "
                         "$TRANSLATION_V2 if unset.")
    ap.add_argument("dst", nargs="?", type=pathlib.Path,
                    help="output dir. Defaults to data/translated-tr-in-en/ "
                         "under this repo.")
    args = ap.parse_args(argv)

    import os
    src = args.src or pathlib.Path(os.environ.get("TRANSLATION_V2", ""))
    if not src or not src.is_dir():
        print("transform_translations: no input dir (set $TRANSLATION_V2 or "
              "pass as the first argument)", file=sys.stderr)
        return 2
    dst = args.dst or (pathlib.Path(__file__).resolve().parent.parent
                       / "data" / "translated-tr-in-en")
    dst.mkdir(parents=True, exist_ok=True)

    n = 0
    for path in iter_json(src):
        with path.open(encoding="utf-8") as f:
            doc = json.load(f)
        out_doc = transform_bundle(doc)
        with (dst / path.name).open("w", encoding="utf-8") as f:
            json.dump(out_doc, f, ensure_ascii=False, indent=2)
            f.write("\n")
        n += 1
        print(f"  transformed  {path.name}")
    print(f"\n{n} JSON file(s) written to {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
