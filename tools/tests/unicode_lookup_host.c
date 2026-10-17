/* ILP32 fixture: current utf8.c oracle + product file loader + mock SDK.
 * VFS input bytes are read from the generated file by the Python driver. */
#include <stdarg.h>
#include "unicode_file.h"
#include "config.h"
#include "endian_le.h"
#include "memmap.h"
#include "pgalloc.h"
#include "vfs.h"
#include "kprintf.h"
#include "unicode_lookup_mock.h"
#include "unicode_lookup_oracle.h"

u16 unicode_to_jis(u32 cp);
void utf8_set_jis_table(const u8 *table);

static u32 kapi_calls;
static u16 __attribute__((cdecl)) wrap_unicode_to_jis(u32 cp)
{
    kapi_calls++;
    return unicode_file_lookup(cp);
}
static const struct unicode_mock_api api = { wrap_unicode_to_jis };
const struct unicode_mock_api *unicode_mock_api = &api;

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    u32 ret;
    /* The syscall returns in eax; declare it so the next call reloads 4. */
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
    (void)ret;
}
static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(c, label) do { if (!(c)) { say("FAIL " label "\n"); die(1); } } while (0)

/* The page allocator returns an identity-mapped, page-aligned host buffer.
 * No kernel lookup or validation logic is substituted. */
static u8 pages[7 * MEM_PAGE_SIZE] __attribute__((aligned(MEM_PAGE_SIZE)));
static int stat_rc, read_rc = UNICODE_FILE_BYTES, fail_alloc;
static u32 stat_size = UNICODE_FILE_BYTES;
static u32 alloc_calls, free_calls, stat_calls, read_calls, log_calls, live_pages;
static u32 corrupt_at;
static int corruption; /* 1 = reverse adjacent records, 2 = duplicate key. */
static const char *expected_reason;

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void kana_only(void)
{
    const u32 kana[] = {0x3041, 0x3093, 0x30A1, 0x30F6};
    const u16 jis[] = {0x2421, 0x2473, 0x2521, 0x2576};
    for (u32 i = 0; i < sizeof(kana) / sizeof(kana[0]); i++) {
        CHECK(unicode_file_lookup(kana[i]) == jis[i], "fallback-kana");
        u32 calls = kapi_calls;
        CHECK(sdk_unicode_to_jis(kana[i]) == jis[i], "fallback-sdk");
        CHECK(kapi_calls == calls + 1, "sdk-kapi");
    }
    const u32 absent[] = {0x3040, 0x3094, 0x30A0, 0x30F7, 0, 0xFFFF,
                          0x00A7, 0x4E9C, 0xFFE5, 0x10000, 0xFFFFFFFF};
    for (u32 i = 0; i < sizeof(absent) / sizeof(absent[0]); i++) {
        CHECK(unicode_file_lookup(absent[i]) == 0, "fallback-absent");
        CHECK(sdk_unicode_to_jis(absent[i]) == 0, "fallback-sdk-absent");
    }
}

int vfs_stat(const char *path, OS32_Stat *st)
{
    CHECK(same(path, SYS_UNICODE_BIN), "stat-path");
    stat_calls++;
    st->st_size = stat_size;
    return stat_rc;
}

u32 pgalloc_alloc_phys(u32 owner, int n)
{
    CHECK(owner == LEDGER_OWNER_KERNEL && n == 7, "alloc-owner-pages");
    CHECK(!live_pages, "alloc-leak");
    alloc_calls++;
    if (fail_alloc) return 0;
    live_pages = n;
    /* Poison the 420 bytes beyond the file with nonzero fake pairs. */
    for (u32 i = 0; i < sizeof(pages); i += 4) {
        le16_wr(pages + i, 0xFFFF);
        le16_wr(pages + i + 2, 0x2121);
    }
    return V2P(pages);
}

int vfs_read(const char *path, void *buf, u32 size)
{
    CHECK(same(path, SYS_UNICODE_BIN), "read-path");
    CHECK(buf == pages && size == 28252 && live_pages == 7, "read-buffer");
    CHECK(unicode_file_lookup(0xFFFF) == 0, "read-publication");
    read_calls++;
    /* Even an I/O failure may have written a partial, apparently valid table. */
    u32 copy = read_rc < 0 ? 100 : (u32)read_rc;
    if (copy > size) copy = size;
    for (u32 i = 0; i < copy; i++) pages[i] = generated_pairs[i];
    if (corruption == 1) {
        for (u32 j = 0; j < 4; j++) {
            u8 t = pages[(corrupt_at - 1) * 4 + j];
            pages[(corrupt_at - 1) * 4 + j] = pages[corrupt_at * 4 + j];
            pages[corrupt_at * 4 + j] = t;
        }
    } else if (corruption == 2) {
        le16_wr(pages + corrupt_at * 4, le16_rd(pages + (corrupt_at - 1) * 4));
    }
    CHECK(unicode_file_lookup(0x4E9C) == 0, "read-publication");
    return read_rc;
}

int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    CHECK(owner == LEDGER_OWNER_KERNEL && pfn == V2P(pages) / MEM_PAGE_SIZE &&
          n == 7 && live_pages == 7, "free-owner-pages");
    CHECK(unicode_file_lookup(0x4E9C) == 0, "free-publication");
    free_calls++;
    live_pages = 0;
    return 1;
}

void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    CHECK(attr == 0x07, "diagnostic-attr");
    va_list ap;
    va_start(ap, fmt);
    log_calls++;
    if (expected_reason) {
        CHECK(same(fmt, "[unicode] unavailable reason=%s; kana-only\n"), "load-rejected");
        CHECK(same(va_arg(ap, const char *), expected_reason), "diagnostic-reason");
    } else {
        CHECK(same(fmt, "[unicode] loaded pairs=%u pages=%u pfn=%u\n"), "load-success");
        CHECK(va_arg(ap, u32) == 7063 && va_arg(ap, u32) == 7 &&
              va_arg(ap, u32) == V2P(pages) / MEM_PAGE_SIZE, "diagnostic-loaded");
    }
    va_end(ap);
}

static void rejected(const char *reason, u32 allocs, u32 reads, u32 frees)
{
    expected_reason = reason;
    alloc_calls = free_calls = read_calls = stat_calls = log_calls = 0;
    unicode_file_load();
    CHECK(live_pages == 0, "failed-pages");
    CHECK(alloc_calls == allocs && read_calls == reads && free_calls == frees &&
          stat_calls == 1 && log_calls == 1, "failure-calls");
    kana_only();
    CHECK(log_calls == 1, "lookup-no-log");
}

static void load_failures(void)
{
    kana_only();
    stat_rc = VFS_ERR_NOTFOUND;
    rejected("missing", 0, 0, 0);
    stat_rc = VFS_ERR_IO;
    rejected("io", 0, 0, 0);
    stat_rc = 0;
    const u32 bad_size[] = {0, 28251, 28253, 131072};
    for (u32 i = 0; i < sizeof(bad_size) / sizeof(bad_size[0]); i++) {
        stat_size = bad_size[i];
        rejected("size", 0, 0, 0);
    }
    stat_size = 28252;
    fail_alloc = 1;
    rejected("nomem", 1, 0, 0);
    fail_alloc = 0;
    read_rc = VFS_ERR_IO;
    rejected("io", 1, 1, 1);
    const int bad_read[] = {0, 28251, 28253};
    for (u32 i = 0; i < sizeof(bad_read) / sizeof(bad_read[0]); i++) {
        read_rc = bad_read[i];
        rejected("size", 1, 1, 1);
    }
    read_rc = 28252;
    const u32 positions[] = {1, 3531, 7062};
    for (corruption = 1; corruption <= 2; corruption++) {
        for (u32 i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
            corrupt_at = positions[i];
            rejected("order", 1, 1, 1);
        }
    }
    corruption = 0;
    say("PASS missing/io/size/order/nomem: private until validated, rollback 7 pages, kana-only\n");
}

static void compare(u32 cp, const char *label)
{
    u16 old = unicode_to_jis(cp);
    u32 calls = kapi_calls;
    if (unicode_file_lookup(cp) != old) {
        say("FAIL "); say(label); say(" direct\n"); die(1);
    }
    CHECK(kapi_calls == calls, "kernel-direct");
    if (sdk_unicode_to_jis(cp) != old) {
        say("FAIL "); say(label); say(" sdk\n"); die(1);
    }
    CHECK(kapi_calls == calls + 1, "sdk-kapi");
}

void harness_main(void) __attribute__((used));
void harness_main(void)
{
    CHECK(sizeof(generated_pairs) == 28252, "table-bytes");
    load_failures();
    expected_reason = 0;
    alloc_calls = free_calls = read_calls = stat_calls = log_calls = 0;
    unicode_file_load();
    CHECK(live_pages == 7 && alloc_calls == 1 && free_calls == 0 &&
          read_calls == 1 && stat_calls == 1 && log_calls == 1, "success-calls");
    for (u32 i = 0; i < sizeof(generated_pairs); i++)
        CHECK(pages[i] == generated_pairs[i], "loaded-bytes");
    utf8_set_jis_table(oracle_flat);
    CHECK(unicode_to_jis(0x4E9C) == 0x3021, "oracle-ready");
    compare(0x00A7, "first");
    compare(0xFFE5, "last");
    compare(0, "missing");
    compare(0xFFFF, "missing-last");
    const u32 outside[] = {0x10000, 0x1F600, 0x10FFFF, 0x110000, 0xFFFFFFFF};
    for (u32 i = 0; i < sizeof(outside) / sizeof(outside[0]); i++)
        compare(outside[i], "outside");
    const u32 kana[] = {0x3040, 0x3041, 0x3093, 0x3094,
                        0x30A0, 0x30A1, 0x30F6, 0x30F7};
    for (u32 i = 0; i < sizeof(kana) / sizeof(kana[0]); i++)
        compare(kana[i], "kana");
    CHECK(unicode_to_jis(0x3041) == 0x2421 && unicode_to_jis(0x3093) == 0x2473 &&
          unicode_to_jis(0x30A1) == 0x2521 && unicode_to_jis(0x30F6) == 0x2576,
          "kana-arithmetic");
    for (u32 i = 0; i < 7063; i++) compare(oracle_cp[i], "pair");
    for (u32 cp = 0; cp <= 0xFFFF; cp++) compare(cp, "bmp");
    CHECK(alloc_calls == 1 && free_calls == 0 && read_calls == 1 &&
          stat_calls == 1 && log_calls == 1, "lookup-no-io");
    say("PASS pairs=7063 BMP=65536 first/last/missing/outside/kana direct+SDK/KAPI table=28252B\n");
    die(0);
}
__asm__(".globl _start\n_start:\n"
        "andl $-16, %esp\ncall harness_main\nud2\n");
