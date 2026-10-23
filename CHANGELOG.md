# Changelog

## v0.1.0 — First public release

- 25 OLTX bundles (approximately 5,800 keys) translated to Turkish.
- ASI-based in-memory swap of the game's OLTX string tables via a hook
  on `OLTX_LoadFromStream` and `OLTX_ParseBuffer`; no exe patch, no
  language override, no `.sig` tampering, no DLL injection into
  unrelated processes.
- No Activision assets redistributed.
