# VFS paths of OLTX bundles

Each entry below maps an extracted OLTX bundle (named by its PAK2 chunk-id and
content hash) to its likely VFS path in the game. Mapping is derived from:

- The exe's string table has two explicit `.loc` paths (`gamedata\xt\Loc\%s.loc`
  and `gamedata\xt\Loc\Common.loc`) plus ~24 `gamedata\Xt\*.bin` paths.
- Each OLTX bundle's key-prefix pattern cleanly identifies its domain.

Confidence legend: ✓ = clear match, ~ = plausible, ? = uncertain.

| Bundle (chunk_hash) | Keys | Key prefix | VFS path | Conf |
|---|---:|---|---|---|
| 002a25_0x00002a25 | 43 | LOC_ (days, months, cash symbol) | `gamedata/xt/Loc/Common.loc` | ✓ |
| 002a26_0x527c6ecf | 1316 | MP_ | `gamedata/Xt/Multiplayer.bin` | ✓ |
| 002aaa_0x527e1a13 | 281 | SP_, POB_, OOO_ | `gamedata/Xt/SinglePlayer.bin` | ✓ |
| 002ac0_0x527e1a13 | 197 | VEH_ | `gamedata/Xt/Vehicles.bin` | ✓ |
| 002aa3_0x00002aa3 | 189 | SCR_ | `gamedata/Xt/Scoring.bin` | ✓ |
| 0029b2_0x000029b2 | 294 | ACH_ | `gamedata/Xt/Achievements.bin` | ✓ |
| 0029ef_0x000029ef | 84 | FBK_ (Facebook) | `gamedata/Xt/SocialNetworking.bin` | ~ |
| 002abc_0x00002abc | 65 | TWT_ (Twitter) | `gamedata/Xt/SocialNetworking.bin` | ~ |
| 0029f6_0x527ddd09 | 43 | BOLT_, NITRO_, BARGE_, MINE_, SHUNT_, RAM_ | `gamedata/Xt/Pickups.bin` | ✓ |
| 0029c4_0x527ddd09 | 833 | CMN_, IDS_, DBG_, PIK_, AIR_, UPD_ | `gamedata/Xt/UI.bin` | ~ |
| 002a1a_0x527e571d | 101 | HUD_ | `gamedata/Xt/UI.bin` | ~ |
| 002a1e_0x527ddd09 | 181 | INFO_ | `gamedata/Xt/UI.bin` | ~ |
| 000231_0x712c748f | 234 | POP_ | `gamedata/Xt/UI.bin` | ~ |
| 002a76_0x527c6ecf | 235 | POP_ (variant of 000231) | `gamedata/Xt/UI.bin` | ~ |
| 000245_0x712cb198 | 172 | HLP_ | `gamedata/Xt/UI.bin` | ~ |
| 002a03_0x00002a03 | 171 | HLP_ (variant of 000245) | `gamedata/Xt/UI.bin` | ~ |
| 002a68_0x527ddd09 | 128 | MHLP_, MLHP_ | `gamedata/Xt/UI.bin`? | ? |
| 002a8c_0x527e1a13 | 333 | RBC_ | ? (RocketBar / community) | ? |
| 002a86_0x527e1a13 | 87 | PRO_ | ? (profile) | ? |
| 002a99_0x527e1a13 | 125 | RBU_, DEMO_, USABILITY_ | ? | ? |
| 002aa1_0x527e1a13 | 33 | RP_ | ? (rich presence) | ? |
| 002a75_0x00002a75 | 4 | POB_, MP_ | ? (DLC / community TMS) | ? |
| 0029c1_0x527e1a13 | 103 | CRN_ (track names) | ? | ? |
| 0029f8_0x527e571d | 252 | FDE_ (fan demand events) | ? | ? |
| 002abb_0x00002abb | 0 | — (stub) | — | — |

## Open question: `English.loc` / per-language file

The exe sprintfs `gamedata\xt\Loc\English.loc` (and French/Italian/German/Spanish
variants). None of our 25 extracted bundles is uniquely tagged as "the English
file"; every bundle carries all 5 languages. Working hypothesis: `English.loc`
is a redirect name — all 5 variant paths resolve to the same OLTX bundle on
disk, and the engine picks the slot inside by current-language 2-char code.

Install strategy: drop the LOC_ bundle (002a25) at BOTH
`gamedata/xt/Loc/Common.loc` AND `gamedata/xt/Loc/<Lang>.loc` for every shipped
`<Lang>`. Costs a few extra copies, catches whichever path the engine actually
calls.

## Install mode

Loose files first — the engine's VFS loads shipped `.level` files as loose,
same VFS mounts both pak and filesystem. Priority (loose-vs-pak) is unverified
from static analysis; test on Wine with a single modded bundle before shipping
the full set. If pak wins, fall back to the vtable Open hook (ASI, Strategy C
in `scratchpad/asi_hooks/`).
