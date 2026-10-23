#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
"""Blur (Bizarre Creations, 2010) OLTX localization-table codec.

OLTX is the container used by Blur's ``*.loc`` files to bundle the same string
key set under several language codes.  It is read in-engine by
``Blur.exe!LoadLocFile`` at VA ``0x00C0C050`` which maps a key hash to a
UTF-16LE value from the currently selected language slot.

File layout (all little-endian):

    0x00  u32  magic              == 0x58544C4F ('OLTX')
    0x04  u32  version            == 1
    0x08  u32  header_size        == 0x10, offset of start of section A
    0x0C  u32  section_A_size     bytes of (lang_meta + lang_table + key_table)
    0x10  u32  lang_meta_size     == 0x0C, (always 12 - header above lang_table)
    0x14  u32  num_languages      number of language slots (5 in all shipped files)
    0x18  u32  lang_table_bytes   == 0x0C + num_languages*0x18
                                   (end of lang_table expressed from 0x10)
    0x1C  LangEntry[num_languages] (0x18 bytes each)
    ...   key_table   (nkeys * 8 bytes of {u32 hash, u32 name_offset}
                      sorted alphabetically by name, then name pool)
    ...   LangBlock[num_languages] (packed back-to-back in slot order)

LangEntry (24 bytes):

    0x00  char[16] code           ASCII, NUL-padded (e.g. "en", "fr", "pl")
    0x10  u32      block_offset   absolute file offset of this slot's LangBlock
    0x14  u32      block_size     byte length of that LangBlock

LangBlock:

    0x00  u32  nentries           == same key count as the key_table
    0x04  u32  entry_size         == 8
    0x08  Entry[nentries] (0x08 bytes each: u32 key_hash, u32 string_offset)
          -- entries are sorted by key_hash ascending
          -- string_offset is relative to the LangBlock start
    ...   UTF-16LE string pool    NUL-terminated, serially packed (no dedup).

Hashes in the ``key_hash`` column are a Bizarre-proprietary 32-bit hash whose
algorithm this module does not reproduce.  We store and preserve the hashes
verbatim.  Round-tripping only requires that the same hash points at the same
UTF-16 string on write-back; it does not need the hash function.

Usage::

    tbl  = oltx.decode(open(path, 'rb').read())      # dict-like
    data = oltx.encode(tbl)                          # bytes

    # CLI:
    #   python oltx.py decode  <in.oltx> [<out.json>]
    #   python oltx.py encode  <in.json> <out.oltx>
    #   python oltx.py roundtrip <in.oltx>

"""
from __future__ import annotations

import json
import os
import struct
import sys
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

OLTX_MAGIC = 0x58544C4F        # 'OLTX' little-endian
OLTX_VERSION = 1
HEADER_SIZE = 0x10             # bytes before the lang-meta header
LANG_META_SIZE = 0x0C          # the gap between header_size and the first LangEntry
LANG_ENTRY_SIZE = 0x18         # 24
KEY_ENTRY_SIZE = 0x08
LANG_SUB_HEADER = 0x08         # (nentries, entry_size) at top of each LangBlock


# --------------------------------------------------------------------------- #
#  Data model
# --------------------------------------------------------------------------- #


@dataclass
class OltxTable:
    """A parsed OLTX file.

    Attributes
    ----------
    version:
        Container version. Only 1 is attested.
    language_codes:
        List of language slot codes, in the order they appear in the file
        (e.g. ``['en', 'fr', 'de', 'it', 'es']``).
    keys:
        List of key names in the order they appear in the on-disk key pool
        (alphabetical in every shipped file).
    key_hashes:
        List, one per key, of the 32-bit Bizarre hash stored in the key_table.
        Preserved verbatim; order matches ``keys``.
    strings:
        Mapping ``{lang_code: [value_for_key_0, value_for_key_1, ...]}``.
        Each value is a ``str`` (UTF-16LE on disk, null-terminated).
    hash_order:
        Mapping ``{lang_code: [key_index_in_block_order, ...]}``.  Each list is
        a permutation of ``range(len(keys))`` giving the on-disk order of
        entries in that LangBlock (always sorted by key_hash ascending in
        shipped files, but preserved per-slot so we can reproduce any oddballs).
    """

    version: int = OLTX_VERSION
    language_codes: List[str] = field(default_factory=list)
    keys: List[str] = field(default_factory=list)
    key_hashes: List[int] = field(default_factory=list)
    strings: Dict[str, List[str]] = field(default_factory=dict)
    hash_order: Dict[str, List[int]] = field(default_factory=dict)

    # dict-like access per language --------------------------------------- #

    def __iter__(self):
        return iter(self.language_codes)

    def __contains__(self, lang):
        return lang in self.strings

    def __getitem__(self, lang) -> "Dict[str, str]":
        values = self.strings[lang]
        return OrderedDict((k, v) for k, v in zip(self.keys, values))

    def items(self):
        for lang in self.language_codes:
            yield lang, self[lang]

    # serialisation ------------------------------------------------------- #

    def to_dict(self) -> dict:
        return {
            "version": self.version,
            "language_codes": list(self.language_codes),
            "keys": list(self.keys),
            "key_hashes": [f"0x{h:08x}" for h in self.key_hashes],
            "strings": {
                lang: list(self.strings[lang]) for lang in self.language_codes
            },
            "hash_order": {
                lang: list(self.hash_order[lang]) for lang in self.language_codes
            },
        }

    @classmethod
    def from_dict(cls, d: dict) -> "OltxTable":
        t = cls()
        t.version = int(d.get("version", OLTX_VERSION))
        t.language_codes = list(d["language_codes"])
        t.keys = list(d["keys"])
        hashes = d["key_hashes"]
        t.key_hashes = [
            int(h, 16) if isinstance(h, str) else int(h) for h in hashes
        ]
        t.strings = {lang: list(d["strings"][lang]) for lang in t.language_codes}
        if "hash_order" in d:
            t.hash_order = {
                lang: list(d["hash_order"][lang]) for lang in t.language_codes
            }
        else:
            # Default: entries sorted by hash ascending.
            order = sorted(range(len(t.keys)), key=lambda i: t.key_hashes[i])
            t.hash_order = {lang: list(order) for lang in t.language_codes}
        return t


# --------------------------------------------------------------------------- #
#  Decoder
# --------------------------------------------------------------------------- #


def _read_cstr16(buf: bytes, offset: int) -> str:
    """Read a NUL-terminated UTF-16LE string from ``buf`` at ``offset``."""
    end = offset
    n = len(buf)
    while end + 1 < n and (buf[end] != 0 or buf[end + 1] != 0):
        end += 2
    return buf[offset:end].decode("utf-16-le")


def _read_cstr(buf: bytes, offset: int) -> str:
    end = buf.index(b"\x00", offset)
    return buf[offset:end].decode("ascii")


def decode(data: bytes) -> OltxTable:
    """Parse an OLTX blob into an :class:`OltxTable`."""
    if len(data) < 0x1C:
        raise ValueError("OLTX too short")

    magic, version, hsize, sec_a, lang_meta, nlangs, lang_tbl_bytes = struct.unpack(
        "<7I", data[:0x1C]
    )
    if magic != OLTX_MAGIC:
        raise ValueError(f"bad magic 0x{magic:08x}, expected OLTX")
    if version != OLTX_VERSION:
        raise ValueError(f"unsupported OLTX version {version}")
    if hsize != HEADER_SIZE or lang_meta != LANG_META_SIZE:
        raise ValueError(
            f"unexpected header: hsize=0x{hsize:x} lang_meta=0x{lang_meta:x}"
        )
    expected = LANG_META_SIZE + nlangs * LANG_ENTRY_SIZE
    if lang_tbl_bytes != expected:
        raise ValueError(
            f"lang_tbl_bytes 0x{lang_tbl_bytes:x} != expected 0x{expected:x}"
        )

    # --- Language descriptor table ---------------------------------------
    lang_codes: List[str] = []
    lang_slots: List[Tuple[int, int]] = []   # (block_offset, block_size)
    for i in range(nlangs):
        off = 0x1C + i * LANG_ENTRY_SIZE
        code_raw = data[off:off + 16]
        code = code_raw.rstrip(b"\x00").decode("ascii")
        block_off, block_size = struct.unpack("<2I", data[off + 16:off + 24])
        lang_codes.append(code)
        lang_slots.append((block_off, block_size))

    # --- Key table (hash,name_offset pairs then packed ASCII name pool) --
    key_table_start = HEADER_SIZE + lang_tbl_bytes   # == 0x94 for all shipped
    nkeys, kesz = struct.unpack(
        "<2I", data[key_table_start:key_table_start + 8]
    )
    if kesz != KEY_ENTRY_SIZE:
        raise ValueError(f"key entry_size 0x{kesz:x} != 8")
    key_entries_abs = key_table_start + 8
    key_hashes: List[int] = []
    key_name_offs: List[int] = []
    for i in range(nkeys):
        o = key_entries_abs + i * KEY_ENTRY_SIZE
        h, no = struct.unpack("<2I", data[o:o + 8])
        key_hashes.append(h)
        key_name_offs.append(no)
    # Resolve names.  name_offset is relative to key_table_start.
    keys: List[str] = [
        _read_cstr(data, key_table_start + no) for no in key_name_offs
    ]

    # --- Per-language blocks ---------------------------------------------
    strings: Dict[str, List[str]] = {}
    hash_order: Dict[str, List[int]] = {}

    hash_to_idx: Dict[int, int] = {h: i for i, h in enumerate(key_hashes)}
    if len(hash_to_idx) != len(key_hashes):
        raise ValueError("duplicate key_hash in key_table")

    for (code, (bo, bz)) in zip(lang_codes, lang_slots):
        bnk, besz = struct.unpack("<2I", data[bo:bo + 8])
        if besz != KEY_ENTRY_SIZE:
            raise ValueError(f"lang {code!r} entry_size 0x{besz:x} != 8")
        if bnk != nkeys:
            raise ValueError(
                f"lang {code!r} has {bnk} entries, key_table has {nkeys}"
            )
        order: List[int] = []
        values: List[Optional[str]] = [None] * nkeys
        for i in range(bnk):
            eo = bo + 8 + i * KEY_ENTRY_SIZE
            kh, string_off = struct.unpack("<2I", data[eo:eo + 8])
            try:
                idx = hash_to_idx[kh]
            except KeyError:
                raise ValueError(
                    f"lang {code!r}: unknown key hash 0x{kh:08x}"
                )
            order.append(idx)
            values[idx] = _read_cstr16(data, bo + string_off)
        if any(v is None for v in values):
            raise ValueError(f"lang {code!r} missing values for some keys")
        strings[code] = values  # type: ignore[assignment]
        hash_order[code] = order

    return OltxTable(
        version=version,
        language_codes=lang_codes,
        keys=keys,
        key_hashes=key_hashes,
        strings=strings,
        hash_order=hash_order,
    )


# --------------------------------------------------------------------------- #
#  Encoder
# --------------------------------------------------------------------------- #


def encode(table: OltxTable) -> bytes:
    """Serialize an :class:`OltxTable` back to an OLTX blob."""
    nlangs = len(table.language_codes)
    nkeys = len(table.keys)
    if len(table.key_hashes) != nkeys:
        raise ValueError("key_hashes length mismatch")
    for lang in table.language_codes:
        if lang not in table.strings:
            raise ValueError(f"missing strings for lang {lang!r}")
        if len(table.strings[lang]) != nkeys:
            raise ValueError(
                f"lang {lang!r} has {len(table.strings[lang])} values, "
                f"expected {nkeys}"
            )
        if lang not in table.hash_order:
            # default: sort by hash ascending
            table.hash_order[lang] = sorted(
                range(nkeys), key=lambda i: table.key_hashes[i]
            )
        if sorted(table.hash_order[lang]) != list(range(nkeys)):
            raise ValueError(f"hash_order for {lang!r} is not a permutation")

    lang_tbl_bytes = LANG_META_SIZE + nlangs * LANG_ENTRY_SIZE  # == 0x0C+5*24

    # --- Build key_table ------------------------------------------------- #
    # Lay out: 8-byte (nkeys, esz) header, then nkeys*8 of (hash, name_off),
    # then NUL-terminated key names serially.  name_off is relative to the
    # start of the key_table (where the (nkeys, esz) pair sits).
    key_names_blob = bytearray()
    name_offs: List[int] = []
    pool_base = 8 + nkeys * KEY_ENTRY_SIZE
    cur = pool_base
    for name in table.keys:
        name_offs.append(cur)
        encoded = name.encode("ascii") + b"\x00"
        key_names_blob += encoded
        cur += len(encoded)
    key_table_blob = bytearray()
    key_table_blob += struct.pack("<2I", nkeys, KEY_ENTRY_SIZE)
    # Shipped OLTX files store the top-level key_table sorted alphabetically
    # by key name (not by hash) — the engine binary-searches it by name at
    # lookup time.  The name pool is already laid out in `table.keys` order
    # (alphabetical in every shipped file); the (hash, name_off) entries
    # point into it in that same alphabetical-by-name order.
    key_table_perm = sorted(range(nkeys), key=lambda i: table.keys[i])
    for i in key_table_perm:
        key_table_blob += struct.pack(
            "<2I", table.key_hashes[i], name_offs[i]
        )
    key_table_blob += key_names_blob
    key_table_size = len(key_table_blob)

    section_a_size = lang_tbl_bytes + key_table_size

    # --- Lang blocks ----------------------------------------------------- #
    first_lang_block_offset = HEADER_SIZE + section_a_size
    lang_blocks: List[bytes] = []
    lang_block_offsets: List[int] = []
    cur_file_offset = first_lang_block_offset

    for code in table.language_codes:
        order = table.hash_order[code]
        values = table.strings[code]
        header = struct.pack("<2I", nkeys, KEY_ENTRY_SIZE)
        pool_rel_base = len(header) + nkeys * KEY_ENTRY_SIZE  # == 8 + nkeys*8
        # Lay out the pool first so we know each entry's string offset.
        pool = bytearray()
        string_offs_rel: Dict[int, int] = {}      # key_index -> rel offset
        for key_idx in order:
            string_offs_rel[key_idx] = pool_rel_base + len(pool)
            pool += values[key_idx].encode("utf-16-le") + b"\x00\x00"
        entries = bytearray()
        for key_idx in order:
            entries += struct.pack(
                "<2I", table.key_hashes[key_idx], string_offs_rel[key_idx]
            )
        block = bytes(header + entries + pool)
        lang_blocks.append(block)
        lang_block_offsets.append(cur_file_offset)
        cur_file_offset += len(block)

    # --- Fixed header + lang descriptors --------------------------------- #
    out = bytearray()
    out += struct.pack(
        "<7I",
        OLTX_MAGIC,
        table.version,
        HEADER_SIZE,
        section_a_size,
        LANG_META_SIZE,
        nlangs,
        lang_tbl_bytes,
    )
    assert len(out) == 0x1C
    for code, bo, blk in zip(
        table.language_codes, lang_block_offsets, lang_blocks
    ):
        code_bytes = code.encode("ascii")
        if len(code_bytes) > 16:
            raise ValueError(f"lang code {code!r} too long (max 16 chars)")
        padded = code_bytes + b"\x00" * (16 - len(code_bytes))
        out += padded + struct.pack("<2I", bo, len(blk))

    # --- Append section A (key_table) and lang blocks -------------------- #
    out += key_table_blob
    assert len(out) == first_lang_block_offset
    for blk in lang_blocks:
        out += blk
    return bytes(out)


# --------------------------------------------------------------------------- #
#  CLI
# --------------------------------------------------------------------------- #


def _cmd_decode(args: List[str]) -> int:
    if not args:
        print("usage: oltx.py decode <in.oltx> [<out.json>]", file=sys.stderr)
        return 2
    inpath = args[0]
    with open(inpath, "rb") as f:
        tbl = decode(f.read())
    payload = tbl.to_dict()
    if len(args) >= 2:
        with open(args[1], "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
    else:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def _cmd_encode(args: List[str]) -> int:
    if len(args) < 2:
        print("usage: oltx.py encode <in.json> <out.oltx>", file=sys.stderr)
        return 2
    with open(args[0], "r", encoding="utf-8") as f:
        d = json.load(f)
    tbl = OltxTable.from_dict(d)
    blob = encode(tbl)
    with open(args[1], "wb") as f:
        f.write(blob)
    return 0


def _cmd_roundtrip(args: List[str]) -> int:
    if not args:
        print("usage: oltx.py roundtrip <in.oltx>", file=sys.stderr)
        return 2
    inpath = args[0]
    with open(inpath, "rb") as f:
        original = f.read()
    tbl = decode(original)
    out = encode(tbl)
    if out == original:
        print(f"OK  (byte-exact): {inpath}  ({len(original)} bytes)")
        return 0
    # Diagnostics
    print(f"DIFF: {inpath}  orig={len(original)}  rebuilt={len(out)}")
    # Show the first few differing bytes
    for i in range(min(len(out), len(original))):
        if out[i] != original[i]:
            print(f"  first diff at 0x{i:x}: orig=0x{original[i]:02x}  new=0x{out[i]:02x}")
            print(f"  context orig: {original[max(0,i-8):i+16].hex()}")
            print(f"  context new : {out[max(0,i-8):i+16].hex()}")
            break
    return 1


_COMMANDS = {
    "decode": _cmd_decode,
    "encode": _cmd_encode,
    "roundtrip": _cmd_roundtrip,
}


def main(argv: List[str]) -> int:
    if len(argv) < 2 or argv[1] not in _COMMANDS:
        print("usage: oltx.py {decode|encode|roundtrip} ...", file=sys.stderr)
        return 2
    return _COMMANDS[argv[1]](argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
