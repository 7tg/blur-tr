# blur-tr

Turkish language pack ("yama") for PC Blur (2010, Bizarre Creations /
Activision).

**No Activision assets are redistributed.** The pack ships one ASI shim
(an in-memory hook into the game's own localization parser) plus the
author's own Turkish translation of the game's string tables. The game's
own fonts, audio, executable, images, PAK archives and `.sig` signatures
are not touched and are not shipped here.

Running this pack requires that you own a legal copy of PC Blur. See
`DISCLAIMER.md`.

## Install

```sh
tools/install.sh "/path/to/Blur game folder"
```

Example (native Linux copy under Wine):

```sh
tools/install.sh "/home/you/.wine/drive_c/Program Files (x86)/Activision/Blur"
```

The installer writes exactly two things into your Blur folder:

```
scripts/blur_tr.asi
scripts/blur-tr-oltx/*.oltx       (25 files)
```

Loading needs Ultimate ASI Loader (any recent build) in the game folder —
rename its DLL to `dsound.dll` or `dinput8.dll` next to `Blur.exe` so Blur
picks it up at startup. The loader then auto-loads `scripts/blur_tr.asi`.

No language marker file (`english.txt` / `polish.txt`) is created or
removed. No file under `gamedata/` is touched.

To revert:

```sh
tools/uninstall.sh "/path/to/Blur game folder"
```

## Build

The pack ships pre-built OLTX binaries under `data/oltx-tr/`. To rebuild
from source:

```sh
# Encode data/translated-tr-in-en/*.json -> data/oltx-tr/*.oltx and
# build src/asi/blur_tr.asi
tools/build_pack.sh
```

The ASI build needs the i686-w64-mingw32 toolchain (`sudo pacman -S
mingw-w64-gcc` on Arch, equivalent on other distros).

Edit the translation itself under `data/translated-tr-in-en/*.json` and
re-run `tools/build_pack.sh`.

## How it works

Blur's string tables ship inside OLTX bundles embedded in the game's PAK
archives. Each bundle holds the same key set under several language slots
(`en`, `fr`, `de`, `it`, `es`) and the engine picks a slot by comparing a
two-letter language code against the slot name (case-insensitive). On an
English install the engine selects the `en` slot.

`blur_tr.asi` hooks the engine's OLTX stream parser. When Blur parses one
of the bundles that this pack covers, the ASI identifies it by content
fingerprint (`num_keys`, min key hash), frees the parser's buffers, and
feeds the engine the pack's own OLTX bytes instead. The engine then reads
the Turkish strings from the `en` slot of the replacement bundle as if
they were the original English — no language override, no font swap, no
exe patch, no `.sig` tampering.

The engine's English font is Latin-1, which cannot render Turkish
`ıİğĞşŞ`. The build pipeline folds those to their ASCII bases
(`iIgGsS`); diacritics on characters that ARE in Latin-1 (`çöüÇÖÜ`) are
kept.

The translation covers ~5,800 keys across 25 OLTX bundles. Format tokens,
placeholder tags and colour codes are preserved verbatim. Proper nouns
(car brands, mode names, place names) stay in English per the glossary.

## Pack contents

```
src/asi/blur_tr.c             -- ASI shim source (built by `make`)
src/asi/Makefile              -- MinGW-w64 cross-build
tools/oltx.py                 -- OLTX codec (encode / decode / roundtrip)
tools/transform_translations.py
                              -- 6-slot JSON (Turkish in pl) -> en-slot
                                 with ASCII fold + glossary
tools/build_pack.sh           -- rebuild data/oltx-tr/*.oltx + ASI
tools/install.sh              -- stage pack into a Blur folder
tools/uninstall.sh            -- revert what install.sh did
data/translated-tr-in-en/     -- 25 JSON string tables (committed source)
data/oltx-tr/                 -- 25 OLTX binaries, encoded from the JSONs
docs/vfs_paths.md             -- notes on each bundle's content domain
LICENSE                       -- Apache-2.0 (code)
LICENSE-translations          -- CC BY-SA 4.0 (translation strings)
DISCLAIMER.md                 -- unofficial fan work; trademarks
CHANGELOG.md
```

## Testing recipe

1. Install into a clean copy of Blur that already has Ultimate ASI Loader.
2. Launch under Wine (32-bit prefix) or native Windows.
3. Expected: menu text, popups, achievements, multiplayer screens, car info
   all in Turkish; proper nouns remain English.
4. Diagnostic log: `<game>/scripts/blur_tr.log` records each OLTX bundle the
   ASI swapped at parse time. If a screen is still English, note which
   keys appear there and consult `docs/vfs_paths.md` for the bundle whose
   keys match.

## Non-goals

- No Activision asset redistribution (fonts, audio, images, exe, PAK archives).
- No `.sig` tampering, no exe patching, no DLL injection into unrelated
  processes.
- No language override — the engine stays in English mode, we just hand it
  Turkish strings for its English slot.
