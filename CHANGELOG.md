# Changelog

## v0.1.1 — Crash fix

- Fixed a crash right after the first swapped string table (unhandled
  exception 0xC0000417, an invalid C runtime parameter). The ASI passed
  `OLTX_LoadFromStream`'s stream argument to `OLTX_ParseBuffer` as the
  language code; when the engine reads through its default stream that
  argument is NULL, and `_stricmp` on it ended the game. It now passes the
  language code itself.

## v0.1.0 — First public release

- 25 OLTX bundles (approximately 5,800 keys) translated to Turkish.
- ASI-based in-memory swap of the game's OLTX string tables via a hook
  on `OLTX_LoadFromStream` and `OLTX_ParseBuffer`; no exe patch, no
  language override, no `.sig` tampering, no DLL injection into
  unrelated processes.
- No Activision assets redistributed.
