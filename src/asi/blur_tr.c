/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Barbaros Goeren <tayyipgoren@gmail.com>.  See LICENSE.
 *
 * blur_tr.asi — Turkish localization shim for PC Blur (2010).
 *
 * Loaded by Ultimate ASI Loader (via dsound.dll in the game folder) at process
 * start.  All work happens on a worker thread spawned from DllMain so the
 * loader is not blocked.
 *
 * Three-hook architecture:
 *   1. Patch Blur.exe!DetectLanguage to return 8 (the Polish slot — into which
 *      we have written our Turkish strings).  Replaces the function body with
 *      `mov eax, 8; ret 8`.
 *   2. Continuous LangId heartbeat on a background thread: VirtualProtect +
 *      write the LangId global whenever something sets it off 8.  Cheap: only
 *      touches memory when the value drifts.
 *   3. Hook Blur.exe!OLTX_LoadFromStream (NOT OLTX_ParseBuffer — that one is
 *      only called for direct-buffer parses which the engine does not use for
 *      pak content; everything in paks is streamed through LoadFromStream).
 *      Strategy: let the engine's own stream parser run, then fingerprint
 *      the parsed in-memory layout by (num_keys, first_key_hash) read from
 *      this+0x1C and *(this+0x24)+8.  Match → free the LFS-allocated lang
 *      and key buffers through the engine's free (VA 0x42C8A0) and feed our
 *      Turkish bytes through the native OLTX_ParseBuffer, which repopulates
 *      the same ten this-struct fields with the same semantics.  No match →
 *      return the engine's stream parse unmodified.
 *      (Why not match on cache_entry[0x120]?  The value there is a sequential
 *      pak-chunk index — 0,1,2,… — not the semantic 0x2xx/0x29xx hash we
 *      derive from filenames.  Content fingerprinting after the parse is the
 *      only reliable identifier.)
 *   4. Defensive hook on OLTX_ParseBuffer that fingerprint-matches by
 *      (num_langs, num_keys, first_key_hash) is retained in case another code
 *      path invokes ParseBuffer directly.
 *
 * Addresses, resolved against GetModuleHandleA(NULL) at runtime (ASLR-safe):
 *
 *   LangId  (int32)           VA 0x0115ED9C   (-> IB offset 0x00D5ED9C)
 *   DetectLanguage            VA 0x0073D1E0   (-> IB offset 0x0033D1E0)
 *   OLTX_ParseBuffer          VA 0x0073EE90   (-> IB offset 0x0033EE90)
 *   OLTX_LoadFromStream       VA 0x0073F190   (-> IB offset 0x0033F190)
 *
 * Prologue bytes we overwrite (confirmed with i686-w64-mingw32-objdump):
 *
 *   DetectLanguage@0x73D1E0 :  81 EC 60 01 00 00 55 56
 *     -> replaced with         B8 08 00 00 00 C2 08 00
 *                                mov eax, 8; ret 8
 *
 *   OLTX_ParseBuffer@0x73EE90 :  53 8B 5C 24 08
 *     -> replaced with           E9 <rel32>
 *     (5-byte inline trampoline — all 5 bytes are relocation-free.)
 *
 *   OLTX_LoadFromStream@0x73F190 :  8B 44 24 0C 83 EC 1C
 *     -> replaced with              E9 <rel32> 90 90
 *     (7-byte prologue; the 5-byte JMP replaces the first 5 bytes and the
 *     remaining 2 bytes are NOPped.  `mov eax, [esp+c]` and `sub esp, 0x1c`
 *     are both relocation-free so the trampoline can replay them verbatim
 *     before jumping back to original+7.)
 *
 * Co-existence: no system-wide hooks, nothing touched outside Blur.exe's
 * image, no shared files beyond the log.  Co-exists with any other ASI
 * loaded through Ultimate ASI Loader (no shared hooks, no global patches
 * outside Blur.exe's image).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------- offsets ---------------------------------- */

#define OFF_LANG_ID             0x00D5ED9CU   /* VA 0x0115ED9C - 0x400000 */
#define OFF_DETECT_LANGUAGE     0x0033D1E0U   /* VA 0x0073D1E0 - 0x400000 */
#define OFF_OLTX_PARSE          0x0033EE90U   /* VA 0x0073EE90 - 0x400000 */
#define OFF_OLTX_LOAD_STREAM    0x0033F190U   /* VA 0x0073F190 - 0x400000 */
#define OFF_OLTX_SLOT_SELECT    0x0033EC50U   /* VA 0x0073EC50 - 0x400000 */
#define OFF_ENGINE_FREE         0x0002C8A0U   /* VA 0x0042C8A0 - 0x400000 */

/*
 * LangId writers in Blur.exe — the exhaustive static scan.
 *
 * An opcode-level scan of the shipped Blur.exe .text section for every
 * occurrence of the 4-byte little-endian LangId address (`9c ed 15 01`)
 * finds exactly 8 references, classified by the preceding opcode byte(s):
 *
 *   VA 0x0077902C   83 3d 9c ed 15 01 0d       cmpl $0xd, [LangId]   (read)
 *   VA 0x00779164   83 3d 9c ed 15 01 0d       cmpl $0xd, [LangId]   (read)
 *   VA 0x007855AF   83 3d 9c ed 15 01 0c       cmpl $0xc, [LangId]   (read)
 *   VA 0x00B4A29B   a1 9c ed 15 01             mov  eax, [LangId]    (read)
 *   VA 0x00C34806   8b 0d 9c ed 15 01          mov  ecx, [LangId]    (read)
 *   VA 0x00C3BB20   a1 9c ed 15 01             mov  eax, [LangId]    (read)
 *   VA 0x00C49E37   83 3d 9c ed 15 01 07       cmpl $0x7, [LangId]   (read)
 *   VA 0x00E278D1   c7 05 9c ed 15 01 01…      movl $1,  [LangId]    (write)
 *
 * A follow-up scan for any instruction that could load the LangId address
 * into a register for an indirect write — `push imm32`, `lea r32, [imm32]`,
 * `mov r32, imm32`, `mov [imm32], eax (a3)`, `mov [imm32], r32 (89 05/0d/…)` —
 * returns zero matches.  So there is exactly ONE direct writer in the whole
 * binary: the static initializer at VA 0x00E278D1 (image_base+0xA278D1).  Its
 * 10-byte opcode is `c7 05 9c ed 15 01 01 00 00 00` — `movl $0x1, 0x115ED9C`.
 *
 * patch_lang_id_writer() below is a table-driven NOP applier so adding a
 * writer later is a one-line change: append an entry and the init loop
 * NOPs it (or logs a byte mismatch).  Reads are not touched: when LangId
 * holds our target slot (8), those cmpl/mov reads naturally see the right
 * value.
 */
struct writer_entry {
    uint32_t    ib_offset;         /* bytes past g_image_base */
    size_t      length;            /* number of bytes to NOP */
    const uint8_t *expected;       /* expected bytes at the site */
    const char *label;             /* short name for the log line */
};

static const uint8_t WRITER1_BYTES[10] = {
    0xC7, 0x05, 0x9C, 0xED, 0x15, 0x01,   /* mov dword [0x115ED9C], imm32 */
    0x01, 0x00, 0x00, 0x00                /*                        = 1   */
};

static const struct writer_entry g_writer_table[] = {
    {
        /* VA 0x00E278D1 — the one direct `movl $0x1, 0x115ED9C`.  Called
         * from the .rdata static-init table at image+0xA5B4E4 before main
         * runs, which would clobber our DllMain stamp before the engine's
         * lang-code resolver reads LangId. */
        .ib_offset = 0x00A278D1U,
        .length    = sizeof WRITER1_BYTES,
        .expected  = WRITER1_BYTES,
        .label     = "static-init movl",
    },
};

#define TARGET_LANG_SLOT        8             /* 'pl' in the shipped slot
                                                 order; the task spec writes
                                                 our strings into that slot. */

/* ----------------------------- globals ---------------------------------- */

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_cs;
static int               g_log_cs_ready = 0;
static uintptr_t         g_image_base = 0;

/* ----------------------------- log -------------------------------------- */

static void log_openv(const char *game_dir)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\scripts\\blur_tr.log", game_dir);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                        NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    InitializeCriticalSection(&g_log_cs);
    g_log_cs_ready = 1;
}

static void log_write(const char *msg)
{
    if (g_log == INVALID_HANDLE_VALUE)
        return;
    DWORD w = 0;
    size_t n = strlen(msg);
    if (g_log_cs_ready)
        EnterCriticalSection(&g_log_cs);
    WriteFile(g_log, msg, (DWORD)n, &w, NULL);
    FlushFileBuffers(g_log);
    if (g_log_cs_ready)
        LeaveCriticalSection(&g_log_cs);
}

static void log_fmt(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    log_write(buf);
}

/* ----------------------------- OLTX layout ------------------------------ */
/*
 * See scratchpad/oltx/oltx.py for the authoritative description.
 *
 *   0x00  u32  magic 'OLTX' (0x58544C4F)
 *   0x04  u32  version      (==1)
 *   0x08  u32  header_size  (==0x10)
 *   0x0C  u32  section_A_size
 *   0x10  u32  lang_meta_size (==0x0C)
 *   0x14  u32  num_languages
 *   0x18  u32  lang_table_bytes (==0x0C + num_languages*0x18)
 *   0x1C  LangEntry[num_languages]  (each 0x18 bytes)
 *   next  u32  num_keys
 *   next  u32  key_entry_size (==0x08)
 *   next  u32  first key hash
 *
 * first_key_hash lives at   0x1C + num_languages*0x18 + 8.
 */

#define OLTX_MAGIC_LE   0x58544C4FU

struct oltx_fp {
    uint32_t num_langs;
    uint32_t num_keys;
    uint32_t first_hash;
};

static int oltx_fingerprint(const uint8_t *buf, size_t size,
                            struct oltx_fp *out)
{
    if (size < 0x1C) return 0;
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != OLTX_MAGIC_LE) return 0;
    uint32_t nlangs;
    memcpy(&nlangs, buf + 0x14, 4);
    if (nlangs > 32) return 0;                /* sanity */
    size_t key_tbl = 0x1C + (size_t)nlangs * 0x18;
    if (size < key_tbl + 8) return 0;
    uint32_t nkeys;
    memcpy(&nkeys, buf + key_tbl, 4);
    if (nkeys == 0) return 0;
    /* Need the entire (hash, name_off) table in-bounds. */
    size_t entries = key_tbl + 8;
    if (size < entries + (size_t)nkeys * 8) return 0;
    /* Compute first_hash = min(hash) over every entry rather than reading
     * buf[key_tbl+8] directly: the engine's runtime fingerprint reads the
     * first element of its sorted-ascending key table, which is the min.
     * By computing min here we match that semantic regardless of the
     * encoder's on-disk ordering — so matching stays correct even if some
     * file is not sorted (and we don't depend on a specific layout). */
    uint32_t hmin;
    memcpy(&hmin, buf + entries, 4);
    for (uint32_t i = 1; i < nkeys; i++) {
        uint32_t h;
        memcpy(&h, buf + entries + (size_t)i * 8, 4);
        if (h < hmin) hmin = h;
    }
    out->num_langs  = nlangs;
    out->num_keys   = nkeys;
    out->first_hash = hmin;
    return 1;
}

/* ----------------------------- replacement table ----------------------- */

struct oltx_entry {
    struct oltx_fp fp;
    uint8_t       *bytes;
    uint32_t       size;
    uint32_t       chunk_id;     /* parsed from the 6-hex filename prefix */
    int            chunk_id_ok;
    char           name[64];
};

static struct oltx_entry  g_entries[64];
static unsigned           g_entries_n = 0;

/* Parse the leading 6 hex digits of a filename like `0029b2_0x000029b2.oltx`
 * into a uint32 chunk id.  Returns 1 on success.  The engine identifies
 * each OLTX bundle in the pak cache by this id (stored at cache_entry[0x120]
 * and used by OLTX_CacheGet's `cmp esi, [ecx+0x120]`).
 */
static int parse_chunk_id(const char *name, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 6; i++) {
        char c = name[i];
        if (!isxdigit((unsigned char)c)) return 0;
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
    }
    if (name[6] != '_') return 0;
    *out = v;
    return 1;
}

static void load_replacement_dir(const char *game_dir)
{
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\scripts\\blur-tr-oltx", game_dir);
    char glob[MAX_PATH];
    snprintf(glob, sizeof glob, "%s\\*.oltx", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        log_fmt("[init] no replacement OLTX files found at %s\n", dir);
        return;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (g_entries_n >= (sizeof g_entries / sizeof g_entries[0])) break;

        char fp[MAX_PATH];
        snprintf(fp, sizeof fp, "%s\\%s", dir, fd.cFileName);

        HANDLE fh = CreateFileA(fp, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fh == INVALID_HANDLE_VALUE) {
            log_fmt("[init]   cannot open %s (err=%lu)\n",
                    fd.cFileName, (unsigned long)GetLastError());
            continue;
        }
        DWORD size = GetFileSize(fh, NULL);
        if (size == 0 || size == INVALID_FILE_SIZE) {
            CloseHandle(fh);
            continue;
        }
        uint8_t *buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, size);
        if (!buf) { CloseHandle(fh); continue; }
        DWORD got = 0;
        if (!ReadFile(fh, buf, size, &got, NULL) || got != size) {
            HeapFree(GetProcessHeap(), 0, buf);
            CloseHandle(fh);
            continue;
        }
        CloseHandle(fh);

        struct oltx_fp fpv;
        if (!oltx_fingerprint(buf, size, &fpv)) {
            log_fmt("[init]   %s: not a valid OLTX, skipped\n", fd.cFileName);
            HeapFree(GetProcessHeap(), 0, buf);
            continue;
        }

        struct oltx_entry *e = &g_entries[g_entries_n++];
        e->fp    = fpv;
        e->bytes = buf;
        e->size  = size;
        e->chunk_id_ok = parse_chunk_id(fd.cFileName, &e->chunk_id);
        snprintf(e->name, sizeof e->name, "%s", fd.cFileName);

        log_fmt("[init]   loaded %s  size=%u  fp=(nl=%u, nk=%u, h0=0x%08x)  "
                "chunk=%s0x%06x\n",
                fd.cFileName, (unsigned)size,
                (unsigned)fpv.num_langs, (unsigned)fpv.num_keys,
                (unsigned)fpv.first_hash,
                e->chunk_id_ok ? "" : "?",
                (unsigned)e->chunk_id);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    log_fmt("[init] replacement table ready: %u entries\n", g_entries_n);
}

static const struct oltx_entry *
match_entry_by_fp(const struct oltx_fp *fp)
{
    for (unsigned i = 0; i < g_entries_n; i++) {
        const struct oltx_entry *e = &g_entries[i];
        if (e->fp.num_langs  == fp->num_langs &&
            e->fp.num_keys   == fp->num_keys &&
            e->fp.first_hash == fp->first_hash)
            return e;
    }
    return NULL;
}

/* Match on (num_keys, first_hash) only — used by the LFS detour, where we
 * fingerprint from the parsed in-memory layout and the num_langs field is
 * not as cheap to recover as the key body's hash.  All 25 shipped Turkish
 * files have distinct (num_keys, first_hash) tuples per the init log, so
 * this 2-tuple is sufficient. */
static const struct oltx_entry *
match_entry_by_nk_h0(uint32_t num_keys, uint32_t first_hash)
{
    for (unsigned i = 0; i < g_entries_n; i++) {
        const struct oltx_entry *e = &g_entries[i];
        if (e->fp.num_keys   == num_keys &&
            e->fp.first_hash == first_hash)
            return e;
    }
    return NULL;
}

/* ----------------------------- OLTX_ParseBuffer hook -------------------- */
/*
 * __thiscall; 3 stack args (ret 0xc).  Signature:
 *     int __thiscall OLTX_ParseBuffer(void *this,
 *                                     const uint8_t *buffer,
 *                                     int arg1_unused,
 *                                     int arg2_locale_ctx);
 *
 * This hook is defensive: in the shipped engine, pak content is streamed
 * via OLTX_LoadFromStream (which does its own parsing); but if some code
 * path invokes ParseBuffer directly on an in-memory buffer we still swap
 * by content fingerprint.
 */

#define THISCALL __attribute__((thiscall))

typedef int THISCALL (*oltx_parse_fn)(void *thisp,
                                      const uint8_t *buffer,
                                      int arg1,
                                      int arg2);

static oltx_parse_fn g_oltx_parse_orig = NULL;   /* trampoline entry */

static int THISCALL oltx_parse_detour(void *thisp,
                                      const uint8_t *buffer,
                                      int arg1,
                                      int arg2)
{
    struct oltx_fp fp;
    if (buffer && oltx_fingerprint(buffer, (size_t)-1, &fp)) {
        const struct oltx_entry *e = match_entry_by_fp(&fp);
        if (e) {
            log_fmt("[parse] fp=(nl=%u, nk=%u, h0=0x%08x) -> swap in %s\n",
                    (unsigned)fp.num_langs, (unsigned)fp.num_keys,
                    (unsigned)fp.first_hash, e->name);
            return g_oltx_parse_orig(thisp, e->bytes, arg1, arg2);
        }
        log_fmt("[parse] fp=(nl=%u, nk=%u, h0=0x%08x) -> no match\n",
                (unsigned)fp.num_langs, (unsigned)fp.num_keys,
                (unsigned)fp.first_hash);
    } else {
        log_fmt("[parse] non-OLTX or null buffer, passthrough\n");
    }
    return g_oltx_parse_orig(thisp, buffer, arg1, arg2);
}

/* ----------------------------- OLTX_LoadFromStream hook ----------------- */
/*
 * __thiscall; 3 stack args (ret 0xc).  Signature from the pseudo:
 *     bool __thiscall OLTX_LoadFromStream(void *this,  // ecx
 *                                         void *arg0_dest_or_unused,
 *                                         void *arg1_stream_result_slot,
 *                                         void *arg2_stream);     // nullable -> LocaleSingleton
 *
 * `this` is a sub-object of OLTX_CacheEntry at offset +0x124.  The parent
 * cache entry has the chunk id at +0x120, so chunk_id = *(this - 4).
 *
 * On a cache miss, OLTX_CacheEntry_Ctor invokes LoadFromStream to populate
 * the sub-object's fields at offsets 0, 4, 8, c, 10, 14, 18, 1c, 20, 24.
 * Both OLTX_LoadFromStream and OLTX_ParseBuffer populate the SAME ten
 * fields with the same semantics (lang-table and key-table buffers,
 * ptrs into them for the entries).  So we can substitute by feeding our
 * Turkish bytes through the original OLTX_ParseBuffer entry point.
 *
 * ParseBuffer and LoadFromStream both pass their arg2 unchanged to the
 * same inner function at VA 0x73EC50 (locale/context argument); so we
 * forward LFS arg2 as ParseBuffer arg2 and the inner call sees the same
 * value either way.
 */

typedef int THISCALL (*oltx_lfs_fn)(void *thisp,
                                    void *arg0,
                                    void *arg1,
                                    void *arg2);

static oltx_lfs_fn g_oltx_lfs_orig = NULL;

/* Engine's cdecl `free` at VA 0x0042C8A0 — the counterpart to the engine's
 * `malloc` at 0x0042C840 which both LoadFromStream and ParseBuffer use to
 * allocate the lang and key body buffers.  We resolve this at startup
 * (relative to the image base) and use it to release the LFS-allocated
 * buffers in `this->0` and `this->4` before ParseBuffer overwrites those
 * pointers with its own allocations. */
typedef void (__cdecl *engine_free_fn)(void *ptr);
static engine_free_fn g_engine_free = NULL;

static int THISCALL oltx_lfs_detour(void *thisp,
                                    void *arg0,
                                    void *arg1,
                                    void *arg2)
{
    /* Read the chunk id out of the parent OLTX_CacheEntry (this - 4).
     * The engine always calls LFS with `this = cache_entry + 0x124`, and
     * cache_entry[0x120] is initialized to arg3 of OLTX_CacheGet by Ctor
     * before the call.  Previously we treated that value as the chunk's
     * semantic id — the live log shows it is a sequential pak index (0,
     * 1, 2, …) rather than our 0x231/0x245/… hashes, so chunk_id is now
     * only logged for correlation and does NOT drive the match. */
    uint32_t chunk_id = *(volatile uint32_t *)((uint8_t *)thisp - 4);

    /* Let the engine fully parse the stream into `this` first.  This
     * populates the ten this-struct fields that match ParseBuffer's output
     * exactly (lang/key body ptrs, end ptrs, counts).  On failure we have
     * nothing to fingerprint, so just return the failure. */
    int lfs_ok = g_oltx_lfs_orig(thisp, arg0, arg1, arg2);
    if (!lfs_ok) {
        log_fmt("[lfs]  chunk=0x%08x this=%p -> stream parse failed, "
                "returning as-is\n", (unsigned)chunk_id, thisp);
        return lfs_ok;
    }

    /* Fingerprint from the parsed data:
     *   num_keys        = *(u32 *)(this + 0x1C)        [LFS line 73f270]
     *   key_body_base   = *(void **)(this + 0x24)      [LFS line 73f27b]
     *   first_key_hash  = *(u32 *)(key_body_base + 8)  [oltx.py: key body is
     *                                                   {u32 num_keys,
     *                                                    u32 entry_size=8,
     *                                                    [u32 hash, u32 off]…}] */
    uint8_t *tp = (uint8_t *)thisp;
    uint32_t  num_keys    = *(uint32_t *)(tp + 0x1C);
    uint8_t  *key_body    = *(uint8_t **)(tp + 0x24);
    uint32_t  first_hash  = 0;
    if (key_body && num_keys > 0)
        first_hash = *(uint32_t *)(key_body + 8);

    const struct oltx_entry *e = match_entry_by_nk_h0(num_keys, first_hash);
    if (!e) {
        log_fmt("[lfs]  chunk=0x%08x this=%p fp=(nk=%u, h0=0x%08x) "
                "-> no Turkish match, keep stream parse\n",
                (unsigned)chunk_id, thisp,
                (unsigned)num_keys, (unsigned)first_hash);
        return lfs_ok;
    }

    log_fmt("[lfs]  chunk=0x%08x this=%p fp=(nk=%u, h0=0x%08x) "
            "-> swap in %s (parse %u bytes via native OLTX_ParseBuffer)\n",
            (unsigned)chunk_id, thisp,
            (unsigned)num_keys, (unsigned)first_hash,
            e->name, (unsigned)e->size);

    /* ParseBuffer will overwrite this->0 (lang body) and this->4 (key body)
     * with fresh allocations without freeing what LFS put there.  Release
     * them through the engine's own free so the heap stays consistent and
     * we don't leak ~tens of KB per swapped bundle across session reloads. */
    void *old_lang = *(void **)(tp + 0x00);
    void *old_keys = *(void **)(tp + 0x04);
    if (g_engine_free) {
        if (old_lang) g_engine_free(old_lang);
        if (old_keys) g_engine_free(old_keys);
    }

    /* Feed our Turkish bytes through the native OLTX_ParseBuffer via its
     * trampoline so the ParseBuffer detour does not re-fingerprint us.
     * ParseBuffer's arg1 is unused; its arg2 is the language code it hands
     * to the slot lookup at VA 0x73EC50 (_stricmp against each slot's code).
     * LoadFromStream takes (name, lang_code, stream) and passes its arg1 to
     * the same lookup (73f243: mov eax,[esp+0x2c]); its arg2 is the stream,
     * NULL when the engine's default stream is used, and _stricmp(.., NULL)
     * aborts the game with 0xC0000417. */
    int ok = g_oltx_parse_orig(thisp, e->bytes, 0, (int)(uintptr_t)arg1);
    if (!ok) {
        log_fmt("[lfs]  OLTX_ParseBuffer returned 0 for %s; "
                "this-struct is now partially overwritten, returning stream "
                "result anyway\n", e->name);
        /* Not much we can do — the original LFS buffers have been freed and
         * ParseBuffer failed partway.  Return the LFS result so the engine
         * at least sees "something loaded"; subsequent lookups into this
         * cache entry may fail cleanly rather than crash. */
        return lfs_ok;
    }
    return 1;
}

/* ----------------------------- raw patches ------------------------------ */

static int write_bytes_rwx(void *dst, const void *src, size_t n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(dst, src, n);
    DWORD tmp;
    VirtualProtect(dst, n, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}

/* Patch DetectLanguage to be a bare `mov eax, 8; ret 8` stub. */
static int patch_detect_language(uintptr_t base)
{
    uint8_t *addr = (uint8_t *)(base + OFF_DETECT_LANGUAGE);
    const uint8_t patch[] = {
        0xB8, (uint8_t)TARGET_LANG_SLOT, 0x00, 0x00, 0x00,  /* mov eax, 8 */
        0xC2, 0x08, 0x00                                     /* ret 8      */
    };
    log_fmt("[patch] DetectLanguage @%p: first8=%02x %02x %02x %02x "
            "%02x %02x %02x %02x\n",
            addr, addr[0], addr[1], addr[2], addr[3],
            addr[4], addr[5], addr[6], addr[7]);
    if (!write_bytes_rwx(addr, patch, sizeof patch)) {
        log_fmt("[patch] VirtualProtect failed for DetectLanguage (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    log_fmt("[patch] DetectLanguage -> `mov eax, %d; ret 8` installed\n",
            TARGET_LANG_SLOT);
    return 1;
}

/* Walk the writer table and NOP each entry whose expected bytes match the
 * live site.  Any mismatch logs and leaves that one site untouched (other
 * entries still process).  Returns the number of entries successfully
 * NOPed. */
static int patch_lang_id_writer(uintptr_t base)
{
    const unsigned n_entries =
        (unsigned)(sizeof g_writer_table / sizeof g_writer_table[0]);
    int landed = 0;

    for (unsigned i = 0; i < n_entries; i++) {
        const struct writer_entry *w = &g_writer_table[i];
        uint8_t *addr = (uint8_t *)(base + w->ib_offset);

        if (memcmp(addr, w->expected, w->length) != 0) {
            /* Log first 10 bytes (clipped to entry length) for diagnosis. */
            char got[80]; int gp = 0;
            for (size_t b = 0; b < w->length && b < 10; b++) {
                int w2 = snprintf(got + gp, sizeof got - gp, "%s%02x",
                                  b ? " " : "", addr[b]);
                if (w2 <= 0) break;
                gp += w2;
            }
            log_fmt("[patch] LangId-writer-%u (%s) @%p: prologue "
                    "mismatch; got %s\n",
                    i + 1, w->label, addr, got);
            continue;
        }

        /* Build a NOP buffer on the stack; 32 is more than any practical
         * writer opcode length. */
        uint8_t nops[32];
        if (w->length > sizeof nops) {
            log_fmt("[patch] LangId-writer-%u (%s): length %u exceeds "
                    "nop buffer; skipped\n",
                    i + 1, w->label, (unsigned)w->length);
            continue;
        }
        memset(nops, 0x90, w->length);

        if (!write_bytes_rwx(addr, nops, w->length)) {
            log_fmt("[patch] LangId-writer-%u (%s) NOP failed (err=%lu)\n",
                    i + 1, w->label, (unsigned long)GetLastError());
            continue;
        }

        log_fmt("[patch] LangId-writer-%u @%p (ib+0x%08x, %u bytes, %s) "
                "NOPed\n",
                i + 1, addr, (unsigned)w->ib_offset,
                (unsigned)w->length, w->label);
        landed++;
    }

    log_fmt("[patch] LangId-writer table: %d/%u entries landed\n",
            landed, n_entries);
    return landed;
}

/*
 * Pin the OLTX per-chunk slot selector to slot 5 (0-indexed), which is the
 * 6th slot in a 6-slot EFIGS+Pl bundle — the one into which our encoder
 * writes Turkish text (via the `pl` ISO-code tag).
 *
 * The function at VA 0x73EC50 is the engine's `OLTX_FindSlotByLangCode`:
 *   this* in ecx -> record struct with [+0x8] = num_slots_desc,
 *                                     [+0xC] = slot_array_base
 *   stack arg[0] = const char *lang_code  (e.g. "en", "fr", … from LocManager)
 *
 *   On stricmp match (slot[i].code == lang_code): return &slot_array[i]
 *   On no match / empty table         : fall back to slot_array[0]
 *
 * LocManager's active lang code is always "en" in our setup (it reads from a
 * default lang-code table at image_base+0xAA09B0 regardless of our LangId
 * patch), so the engine always picks slot 0 (English) and never reads the
 * Turkish bytes we stamped into slot 5.  Pinning this one function removes
 * the selection entirely: every OLTX record lookup returns slot 5's pointer.
 *
 * Patch, 9 bytes:
 *   8B 41 0C          mov  eax, [ecx+0xC]   ; slot_array_base
 *   83 C0 78          add  eax, 0x78        ; + 5 * 0x18 (stride)
 *   C2 04 00          ret  4                ; __thiscall, 1 stack arg
 *
 * The remaining bytes of the original body are left as-is (dead code — the
 * ret above never falls through).  Original prologue begins with the 9-byte
 * sequence `56 8B F1 8B 46 08 85 C0 74` (push esi; mov esi,ecx; mov eax,
 * [esi+8]; test eax,eax; je ...), which we verify before patching.
 */
static int patch_slot_pin(uintptr_t base)
{
    uint8_t *addr = (uint8_t *)(base + OFF_OLTX_SLOT_SELECT);
    const uint8_t expected[9] = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x08, 0x85, 0xC0, 0x74
    };
    const uint8_t patch[9] = {
        0x8B, 0x41, 0x0C,       /* mov eax, [ecx+0xC]  */
        0x83, 0xC0, 0x78,       /* add eax, 0x78       */
        0xC2, 0x04, 0x00        /* ret 4               */
    };

    log_fmt("[patch] SlotSelect @%p: first9=%02x %02x %02x %02x %02x %02x "
            "%02x %02x %02x\n",
            addr, addr[0], addr[1], addr[2], addr[3],
            addr[4], addr[5], addr[6], addr[7], addr[8]);

    if (memcmp(addr, expected, sizeof expected) != 0) {
        log_fmt("[patch] SlotSelect prologue mismatch; not patching\n");
        return 0;
    }

    if (!write_bytes_rwx(addr, patch, sizeof patch)) {
        log_fmt("[patch] VirtualProtect failed for SlotSelect (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    log_fmt("[patch] SlotSelect -> `mov eax,[ecx+0xC]; add eax,0x78; "
            "ret 4` installed (pin to slot 5)\n");
    return 1;
}

/* Also stamp LangId directly for insurance. */
static void force_lang_id(uintptr_t base)
{
    volatile int32_t *lang = (volatile int32_t *)(base + OFF_LANG_ID);
    DWORD old;
    if (VirtualProtect((void*)lang, sizeof *lang,
                       PAGE_READWRITE, &old)) {
        *lang = TARGET_LANG_SLOT;
        DWORD tmp;
        VirtualProtect((void*)lang, sizeof *lang, old, &tmp);
        log_fmt("[patch] LangId @%p stamped to %d\n", lang, TARGET_LANG_SLOT);
    } else {
        log_fmt("[patch] VirtualProtect(LangId) failed (err=%lu)\n",
                (unsigned long)GetLastError());
    }
}

/* Install a 5-byte JMP trampoline on OLTX_ParseBuffer.  First 5 bytes
 * (push ebx; mov ebx, [esp+8]) are relocation-free. */
static int hook_oltx_parse(uintptr_t base)
{
    uint8_t *target = (uint8_t *)(base + OFF_OLTX_PARSE);

    const size_t saved = 5;
    const uint8_t expected[5] = { 0x53, 0x8B, 0x5C, 0x24, 0x08 };
    if (memcmp(target, expected, 5) != 0) {
        log_fmt("[patch] OLTX_ParseBuffer prologue mismatch at %p: "
                "got %02x %02x %02x %02x %02x\n", target,
                target[0], target[1], target[2], target[3], target[4]);
        return 0;
    }

    uint8_t *tramp = (uint8_t *)VirtualAlloc(NULL, 32,
                                              MEM_COMMIT | MEM_RESERVE,
                                              PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        log_fmt("[patch] VirtualAlloc(parse trampoline) failed (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }

    memcpy(tramp, target, saved);
    tramp[saved] = 0xE9;
    int32_t rel_back = (int32_t)((target + saved) - (tramp + saved + 5));
    memcpy(tramp + saved + 1, &rel_back, 4);

    g_oltx_parse_orig = (oltx_parse_fn)tramp;

    uint8_t detour[5];
    detour[0] = 0xE9;
    int32_t rel_fwd = (int32_t)((uint8_t *)oltx_parse_detour - (target + 5));
    memcpy(detour + 1, &rel_fwd, 4);

    if (!write_bytes_rwx(target, detour, sizeof detour)) {
        log_fmt("[patch] write parse detour failed (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }

    log_fmt("[patch] OLTX_ParseBuffer: detour @%p -> %p, trampoline @%p\n",
            target, (void *)oltx_parse_detour, tramp);
    return 1;
}

/* Install a 7-byte prologue trampoline on OLTX_LoadFromStream.
 *   73f190: 8b 44 24 0c    mov eax, [esp+c]    ; 4 bytes
 *   73f194: 83 ec 1c       sub esp, 0x1c       ; 3 bytes  (total 7)
 *   73f197: 56             push esi            ; next instruction
 * Both saved instructions are relocation-free.  We overwrite 7 bytes with
 * `E9 <rel32> 90 90` so the detour lands on an instruction boundary and
 * the trampoline resumes at the following `push esi`. */
static int hook_oltx_lfs(uintptr_t base)
{
    uint8_t *target = (uint8_t *)(base + OFF_OLTX_LOAD_STREAM);

    const size_t saved = 7;
    const uint8_t expected[7] = {
        0x8B, 0x44, 0x24, 0x0C,   /* mov eax, [esp+0xc] */
        0x83, 0xEC, 0x1C          /* sub esp, 0x1c */
    };
    if (memcmp(target, expected, saved) != 0) {
        log_fmt("[patch] OLTX_LoadFromStream prologue mismatch at %p: "
                "got %02x %02x %02x %02x %02x %02x %02x\n", target,
                target[0], target[1], target[2], target[3],
                target[4], target[5], target[6]);
        return 0;
    }

    uint8_t *tramp = (uint8_t *)VirtualAlloc(NULL, 32,
                                              MEM_COMMIT | MEM_RESERVE,
                                              PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        log_fmt("[patch] VirtualAlloc(lfs trampoline) failed (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }

    memcpy(tramp, target, saved);
    tramp[saved] = 0xE9;
    int32_t rel_back = (int32_t)((target + saved) - (tramp + saved + 5));
    memcpy(tramp + saved + 1, &rel_back, 4);

    g_oltx_lfs_orig = (oltx_lfs_fn)tramp;

    uint8_t detour[7];
    detour[0] = 0xE9;
    int32_t rel_fwd = (int32_t)((uint8_t *)oltx_lfs_detour - (target + 5));
    memcpy(detour + 1, &rel_fwd, 4);
    detour[5] = 0x90;   /* NOPs over the leftover 2 bytes of `sub esp,0x1c` */
    detour[6] = 0x90;

    if (!write_bytes_rwx(target, detour, sizeof detour)) {
        log_fmt("[patch] write lfs detour failed (err=%lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }

    log_fmt("[patch] OLTX_LoadFromStream: detour @%p -> %p, trampoline @%p\n",
            target, (void *)oltx_lfs_detour, tramp);
    return 1;
}

/* ----------------------------- game dir --------------------------------- */

static int resolve_game_dir(char out[MAX_PATH])
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 0;
    char *p = strrchr(exe, '\\');
    if (!p) return 0;
    *p = 0;
    strncpy(out, exe, MAX_PATH);
    out[MAX_PATH - 1] = 0;
    return 1;
}

/* ----------------------------- lang id heartbeat ------------------------ */
/*
 * Belt-and-braces fallback: the static NOP of the sole LangId writer kills
 * the known pre-main clobber, but keep polling in case a code path we did
 * not see races a correction before the engine's lang-code resolver reads
 * LangId.  For the first 5 seconds poll at 1 ms (the engine resolves lang
 * code during startup, so this is the window that matters); after that
 * relax to 500 ms for the rest of the session.  Only writes memory on
 * drift, so steady-state cost is just a load per tick.
 */

#define HB_FAST_INTERVAL_MS   1
#define HB_SLOW_INTERVAL_MS   500
#define HB_FAST_WINDOW_MS     5000

static DWORD WINAPI lang_id_heartbeat(LPVOID param)
{
    (void)param;
    volatile int32_t *lang = (volatile int32_t *)(g_image_base + OFF_LANG_ID);
    unsigned corrections = 0;
    DWORD t0 = GetTickCount();
    int in_fast_phase = 1;
    for (;;) {
        if (*lang != TARGET_LANG_SLOT) {
            DWORD old;
            if (VirtualProtect((void*)lang, sizeof *lang,
                               PAGE_READWRITE, &old)) {
                int32_t prev = *lang;
                *lang = TARGET_LANG_SLOT;
                DWORD tmp;
                VirtualProtect((void*)lang, sizeof *lang, old, &tmp);
                corrections++;
                /* Log the first several corrections only; after that stay
                 * quiet to avoid spamming the log over hours of play. */
                if (corrections <= 20) {
                    log_fmt("[hb]   LangId was %d, re-stamped to %d "
                            "(correction #%u)\n",
                            prev, TARGET_LANG_SLOT, corrections);
                } else if (corrections == 21) {
                    log_fmt("[hb]   (further LangId corrections suppressed)\n");
                }
            }
        }
        if (in_fast_phase) {
            if ((DWORD)(GetTickCount() - t0) >= HB_FAST_WINDOW_MS) {
                in_fast_phase = 0;
                log_fmt("[hb]   fast phase ended; relaxing to %u ms interval "
                        "(%u corrections during fast phase)\n",
                        (unsigned)HB_SLOW_INTERVAL_MS, corrections);
            }
            Sleep(HB_FAST_INTERVAL_MS);
        } else {
            Sleep(HB_SLOW_INTERVAL_MS);
        }
    }
    return 0;   /* unreachable */
}

/* ----------------------------- worker ----------------------------------- */

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;
    char gdir[MAX_PATH] = {0};
    resolve_game_dir(gdir);

    log_openv(gdir);
    log_fmt("[init] blur_tr.asi starting; game dir = %s\n", gdir);

    g_image_base = (uintptr_t)GetModuleHandleA(NULL);
    log_fmt("[init] image base = 0x%p\n", (void *)g_image_base);
    log_fmt("[init]   DetectLanguage       VA = 0x%p\n",
            (void *)(g_image_base + OFF_DETECT_LANGUAGE));
    log_fmt("[init]   OLTX_ParseBuffer     VA = 0x%p\n",
            (void *)(g_image_base + OFF_OLTX_PARSE));
    log_fmt("[init]   OLTX_LoadFromStream  VA = 0x%p\n",
            (void *)(g_image_base + OFF_OLTX_LOAD_STREAM));
    log_fmt("[init]   LangId               VA = 0x%p\n",
            (void *)(g_image_base + OFF_LANG_ID));

    load_replacement_dir(gdir);

    /* Resolve the engine's `free` (counterpart to the engine's `malloc` at
     * 0x0042C840 used by OLTX_LoadFromStream and OLTX_ParseBuffer).  The LFS
     * detour needs it to release LFS-allocated lang/key buffers before
     * handing control to ParseBuffer with our bytes. */
    g_engine_free = (engine_free_fn)(g_image_base + OFF_ENGINE_FREE);
    log_fmt("[init]   engine_free          VA = 0x%p\n", (void *)g_engine_free);

    /*
     * Language-override patches disabled.  We now ship Turkish in slot 0
     * (ISO code "en") of each OLTX bundle, which the engine reads natively
     * when the active language is English.  Forcing the active language to
     * Polish (slot 8) was only needed when Turkish lived in the "pl" slot;
     * it also switches voice-over audio to Polish, which is undesirable.
     * Leaving these call sites in place (void-cast) so the implementations
     * stay compiled and reachable if we ever need to re-enable them.
     */
    (void)patch_lang_id_writer;
    (void)patch_detect_language;
    (void)patch_slot_pin;
    (void)force_lang_id;
    (void)lang_id_heartbeat;
    hook_oltx_parse(g_image_base);
    hook_oltx_lfs(g_image_base);

    log_fmt("[init] startup complete; hooks live\n");
    return 0;
}

/* ----------------------------- entry point ------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
