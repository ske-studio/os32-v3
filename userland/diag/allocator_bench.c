/* T3c3 / F-9. Real USER malloc + KAPI forwarding observer, no fault injection.
 * Run without arguments for isolated children and exit-reclamation checks.
 * Format/procedure: tools/accept/t3c3.md. Keep inputs and v=1 stable for c4. */
#include "os32api.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ATTR 0x07
#define PAGE 4096u
#define LARGE_BYTES 65536u
#define SMALL_BYTES PAGE
#define MAX_SMALL 256u
#define LARGE_COUNT 70u
#define OLD_INDEX (LARGE_COUNT - 4u)
#define MAP_RECORDS (MAX_SMALL + LARGE_COUNT + 4u)
#define SELF "/usr/bin/allocator_bench.bin"
extern KernelAPI *kapi;
extern size_t os32_nano_trim(void);
static KernelAPI observed_api, *real_api;
/* Only allocator calls use observed_api; setup calls use real_api directly.
 * flags=0 mappings are secondary arenas; TOPDOWN mappings are large blocks.
 * This is a mapping observer, never a peek into nano's chunk/list layout. */
static struct mapping { uintptr_t base, end; unsigned large; } maps[MAP_RECORDS];
static unsigned map_calls, exact_calls, exact_fail, map_fail, unmap_calls, unmap_full;
static int observer_bad, clock_bad;
static void *blocks[MAX_SMALL];
static unsigned char *seed;
static const char *case_name;
static int child(const char *name);
static int parent(void);
static void *observe_map(u32 bytes, void *hint, u32 flags);
static int observe_unmap(void *base, u32 bytes);
static int snapshot(MemStat *s);
static uint64_t now(void);
static int row(const char *phase, unsigned n, unsigned bytes, int err,
               uint64_t elapsed, unsigned value, int ok);
static int small(unsigned n);
static int threshold(void);
static int holes(int do_realloc);

int main(int argc, char **argv, KernelAPI *api)
{
    real_api = api;
    if (argc == 1) return parent();
    if (argc == 2) return child(argv[1]);
    api->kprintf(ATTR, "F9 v=1 error=usage\n");
    return 2;
}

static int snapshot(MemStat *s)
{
    return real_api->mem_stat(-1, s, sizeof(*s)) == (int)sizeof(*s) &&
        !(s->flags & (MEMSTAT_POISONED | MEMSTAT_HEAP_INVALID));
}

static void *observe_map(u32 bytes, void *hint, u32 flags)
{
    void *p;
    map_calls++;
    if (flags & OS32_MEM_MAP_EXACT) exact_calls++;
    p = real_api->mem_map(bytes, hint, flags);
    if (!p) {
        map_fail++;
        if (flags & OS32_MEM_MAP_EXACT) exact_fail++;
    } else if (flags & OS32_MEM_MAP_EXACT) {
        for (unsigned i = 0; i < MAP_RECORDS; i++)
            if (maps[i].base && maps[i].end == (uintptr_t)p) {
                maps[i].end += bytes;
                break;
            }
    } else {
        unsigned i;
        for (i = 0; i < MAP_RECORDS && maps[i].base; i++) {}
        if (i == MAP_RECORDS) observer_bad = 1;
        else maps[i] = (struct mapping){(uintptr_t)p, (uintptr_t)p + bytes,
                                       !!(flags & OS32_MEM_MAP_TOPDOWN)};
    }
    return p;
}

static int observe_unmap(void *base, u32 bytes)
{
    int rc;
    unmap_calls++;
    rc = real_api->mem_unmap(base, bytes);
    if (rc == OS32_ERR_FULL) unmap_full++;
    if (!rc) {
        uintptr_t end = (uintptr_t)base + bytes;
        for (unsigned i = 0; i < MAP_RECORDS; i++) {
            struct mapping *m = &maps[i];
            if (!m->base || end != m->end || (uintptr_t)base < m->base) continue;
            if ((uintptr_t)base == m->base) m->base = m->end = 0;
            else m->end = (uintptr_t)base; /* trim of a secondary's tail */
            break;
        }
    }
    return rc;
}

static uint64_t now(void)
{
    u32 lo, hi;
    int rc = OS32_ERR_AGAIN;
    for (unsigned i = 0; i < 8 && rc == OS32_ERR_AGAIN; i++)
        rc = real_api->sys_time_now(&lo, &hi);
    if (rc) { clock_bad = 1; return 0; }
    return ((uint64_t)hi << 32) | lo;
}

static int row(const char *phase, unsigned n, unsigned bytes, int err,
               uint64_t elapsed, unsigned value, int ok)
{
    MemStat s = {0};
    unsigned arenas = 1, large = 0;
    for (unsigned i = 0; i < MAP_RECORDS; i++) if (maps[i].base) {
        if (maps[i].large) large++;
        else arenas++;
    }
    ok = snapshot(&s) && !observer_bad && !clock_bad && ok;
    /* CPL3 copies at most 64 bytes of KAPI arguments (attr + fmt included).
     * Keep each call inside that window while preserving one stable row. */
    real_api->kprintf(ATTR, "F9 v=1 case=%s phase=%s n=%u bytes=%u arenas=%u large=%u map=%u",
        case_name, phase, n, bytes, arenas, large, map_calls);
    real_api->kprintf(ATTR, " exact=%u exact_fail=%u map_fail=%u unmap=%u unmap_full=%u extents=%u slots=%u",
        exact_calls, exact_fail, map_fail, unmap_calls, unmap_full,
        (unsigned)s.extents_total, (unsigned)s.extents_free);
    real_api->kprintf(ATTR, " owner=%u errno=%d us_hi=%u us_lo=%u value=%u ok=%u\n",
        (unsigned)s.owner_pages, err, (unsigned)(elapsed >> 32), (unsigned)elapsed,
        value, (unsigned)ok);
    return ok;
}

static int child(const char *name)
{
    int ok;
    case_name = name;
    if (strcmp(name, "small16") && strcmp(name, "small64") && strcmp(name, "small256") &&
        strcmp(name, "threshold") && strcmp(name, "holes") && strcmp(name, "realloc")) return 2;
    /* Initialize CRT before observing. Keep one small sentinel through trim. */
    seed = malloc(1);
    if (!seed) return 1;
    *seed = 0x5a;
    observed_api = *real_api;
    observed_api.mem_map = observe_map;
    observed_api.mem_unmap = observe_unmap;
    kapi = &observed_api;
    if (!strcmp(name, "threshold")) ok = threshold();
    else if (!strcmp(name, "holes")) ok = holes(0);
    else if (!strcmp(name, "realloc")) ok = holes(1);
    else ok = small(!strcmp(name, "small16") ? 16u : !strcmp(name, "small64") ? 64u : MAX_SMALL);
    ok = row("exit", 0, 0, 0, 0, 0, ok && *seed == 0x5a);
    kapi = real_api;
    /* Deliberately leave live/failed-unmap blocks and setup mapping to exit. */
    return ok ? 0 : 1;
}

static int small(unsigned n)
{
    MemStat s;
    uint64_t start, elapsed;
    unsigned count = 0;
    int err, ok;
    if (!snapshot(&s) || real_api->mem_map(PAGE, (void *)(uintptr_t)s.sbrk_heap_limit,
                                          OS32_MEM_MAP_EXACT) != (void *)(uintptr_t)s.sbrk_heap_limit)
        return row("setup", n, SMALL_BYTES, 0, 0, 0, 0);
    /* Block primary growth. Descending secondary maps naturally block each
     * other's EXACT growth; do not change allocator placement or arena size. */
    errno = 0;
    start = now();
    for (; count < n; count++) {
        blocks[count] = malloc(SMALL_BYTES);
        if (!blocks[count]) break;
    }
    err = errno;
    elapsed = now() - start;
    ok = row("alloc", n, SMALL_BYTES, err, elapsed, count, count == n);
    for (unsigned i = 0; i < count; i++) memset(blocks[i], (int)(i ^ 0xa5), SMALL_BYTES);
    for (unsigned i = 0; i < count; i++)
        for (unsigned j = 0; j < SMALL_BYTES; j++)
            if (((unsigned char *)blocks[i])[j] != (unsigned char)(i ^ 0xa5)) ok = 0;
    errno = 0;
    start = now();
    for (unsigned i = 0; i < count; i++) free(blocks[i]);
    err = errno;
    elapsed = now() - start;
    ok = row("free", n, SMALL_BYTES, err, elapsed, count, ok);
    errno = 0;
    start = now();
    unsigned pages = os32_nano_trim();
    err = errno;
    elapsed = now() - start;
    return row("trim", n, SMALL_BYTES, err, elapsed, pages, ok);
}

static int threshold(void)
{
    int ok = 1;
    for (unsigned bytes = LARGE_BYTES - 1u; bytes <= LARGE_BYTES + 1u; bytes++) {
        uint64_t start = now();
        errno = 0;
        unsigned char *p = malloc(bytes);
        int err = errno;
        uint64_t elapsed = now() - start;
        ok = row("alloc", 1, bytes, err, elapsed, p != NULL, ok && p != NULL);
        if (!p) return 0;
        memset(p, 0x69, bytes);
        for (unsigned j = 0; j < bytes; j++) if (p[j] != 0x69) ok = 0;
        errno = 0;
        start = now();
        free(p);
        err = errno;
        elapsed = now() - start;
        ok = row("free", 1, bytes, err, elapsed, 0, ok);
    }
    return ok;
}

static int holes(int do_realloc)
{
    MemStat before, after;
    unsigned count, failed = 0;
    int ok, err;
    for (count = 0; count < LARGE_COUNT; count++) {
        blocks[count] = malloc(LARGE_BYTES);
        if (!blocks[count]) return row("alloc", LARGE_COUNT, LARGE_BYTES, errno, 0, count, 0);
        memset(blocks[count], (int)(count ^ 0x3c), LARGE_BYTES);
    }
    ok = row("alloc", count, LARGE_BYTES, 0, 0, count, 1);
    /* Ascending odd indices are interior holes in one descending ANON run.
     * Stop on the first real EFULL; keep the unreturned pointer in blocks[]. */
    for (unsigned i = 1; i < LARGE_COUNT - 2u; i += 2) {
        if (!snapshot(&before)) return 0;
        unsigned full_before = unmap_full;
        errno = 0;
        uint64_t start = now();
        free(blocks[i]);
        err = errno;
        uint64_t elapsed = now() - start;
        if (unmap_full != full_before) {
            failed = i;
            ok = row("efull", count, LARGE_BYTES, err, elapsed, i,
                ok && snapshot(&after) && after.extents_free == 0 &&
                after.extents_total == before.extents_total && after.owner_pages == before.owner_pages &&
                err == ENOMEM);
            break;
        }
        blocks[i] = NULL;
    }
    if (!failed) return row("efull", count, LARGE_BYTES, 0, 0, 0, 0);
    if (do_realloc) {
        unsigned char *old = blocks[OLD_INDEX];
        unsigned full_before = unmap_full, large_before = 0;
        for (unsigned i = 0; i < MAP_RECORDS; i++)
            if (maps[i].base && maps[i].large) large_before++;
        if (!snapshot(&before)) return 0;
        errno = 0;
        uint64_t start = now();
        unsigned char *p = realloc(old, LARGE_BYTES + PAGE);
        err = errno;
        uint64_t elapsed = now() - start;
        ok = ok && p && unmap_full == full_before + 1 && err == ENOMEM;
        if (p) for (unsigned j = 0; j < LARGE_BYTES; j++)
            if (p[j] != (unsigned char)(OLD_INDEX ^ 0x3c)) ok = 0;
        ok = row("realloc", count, LARGE_BYTES + PAGE, err, elapsed, large_before + 1u,
            ok && snapshot(&after) && after.extents_free == 0 && after.owner_pages > before.owner_pages);
        /* Repeating free at full capacity exercises the retained list entry.
         * A missing entry would yield EINVAL, and no unmap syscall. */
        full_before = unmap_full;
        errno = 0;
        free(old);
        err = errno;
        ok = row("retained", count, LARGE_BYTES, err, 0, OLD_INDEX,
            ok && err == ENOMEM && unmap_full == full_before + 1u);
    }
    /* Only inspect pointers whose unmap did not succeed, including old. */
    for (unsigned i = 0; i < count; i++) if (blocks[i])
        for (unsigned j = 0; j < LARGE_BYTES; j++)
            if (((unsigned char *)blocks[i])[j] != (unsigned char)(i ^ 0x3c)) ok = 0;
    return row("data", count, LARGE_BYTES, 0, 0, failed, ok);
}

static int parent(void)
{
    static const char *const names[] = {"small16", "small64", "small256", "threshold", "holes", "realloc"};
    static const char *const commands[] = {
        SELF " small16", SELF " small64", SELF " small256",
        SELF " threshold", SELF " holes", SELF " realloc"
    };
    int all_ok = 1;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        MemStat before = {0}, after = {0};
        int kind = EXEC_KIND_NONE, code = -1;
        if (!snapshot(&before)) return 1;
        int rc = real_api->exec_run(commands[i]);
        int ok = real_api->exec_last_result(&kind, &code) == 0 && snapshot(&after);
        ok = ok && !rc && kind == EXEC_KIND_EXITED && !code &&
            before.phys_free_pages == after.phys_free_pages && before.owner_pages == after.owner_pages &&
            before.kheap_used == after.kheap_used && before.resident_heap_used == after.resident_heap_used &&
            before.extents_total == after.extents_total && before.exec_heap_used == after.exec_heap_used &&
            before.leftover_pages == after.leftover_pages && after.reclaim_count > before.reclaim_count;
        real_api->kprintf(ATTR, "F9 v=1 case=%s phase=reclaim rc=%d kind=%d code=%d pool_before=%u pool_after=%u owner_before=%u owner_after=%u leftover_before=%u leftover_after=%u ok=%u\n",
            names[i], rc, kind, code, (unsigned)before.phys_free_pages, (unsigned)after.phys_free_pages,
            (unsigned)before.owner_pages, (unsigned)after.owner_pages,
            (unsigned)before.leftover_pages, (unsigned)after.leftover_pages, (unsigned)ok);
        if (!ok) all_ok = 0;
    }
    real_api->kprintf(ATTR, "F9 v=1 phase=result ok=%u\n", (unsigned)all_ok);
    return all_ok ? 0 : 1;
}
