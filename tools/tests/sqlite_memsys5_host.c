/* Independent observer: exercise public SQLite allocation and SQL entrypoints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite_memsys5_proto.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #x); exit(1); } } while (0)
static int file_opens;

/* No filesystem fixture is needed: all SQL is :memory: and TEMP_STORE=3. */
static int no_open(sqlite3_vfs *v, const char *p, sqlite3_file *f, int flags, int *out)
{
    (void)v; (void)p; (void)f; (void)flags; (void)out;
    file_opens++;
    return SQLITE_CANTOPEN;
}
static int full_path(sqlite3_vfs *v, const char *p, int n, char *out)
{
    (void)v;
    if ((int)strlen(p) >= n) return SQLITE_CANTOPEN;
    strcpy(out, p);
    return SQLITE_OK;
}
static int random_bytes(sqlite3_vfs *v, int n, char *out)
{
    (void)v;
    memset(out, 0x42, n);
    return n;
}
static int current_time(sqlite3_vfs *v, double *out)
{
    (void)v;
    *out = 2460000.5;
    return SQLITE_OK;
}
int sqlite3_os_init(void)
{
    static sqlite3_vfs vfs = {
        .iVersion = 1, .szOsFile = sizeof(sqlite3_file), .mxPathname = 256,
        .zName = "memsys5-host", .xOpen = no_open, .xFullPathname = full_path,
        .xRandomness = random_bytes, .xCurrentTime = current_time
    };
    return sqlite3_vfs_register(&vfs, 1);
}
int sqlite3_os_end(void) { return SQLITE_OK; }
int sqlite3DbIsNamed(void *db, int i, const char *name)
{
    (void)db; (void)name;
    return i == 0; /* Same OMIT_ATTACH bridge as os32_sqlite_vfs.c. */
}

static int total(P5Stats s) { return s.used[0] + s.used[1] + s.used[2]; }
static int power_round(int n)
{
    int r = 32;
    while (r < n) r *= 2;
    return r;
}
static int charge(int mode, int n)
{
    return power_round(n + (mode == P5_PREFIX ? P5_PREFIX_BYTES : 0));
}
static void same_allocations(P5Stats a, P5Stats b)
{
    CHECK(a.live == b.live);
    for (int i = 0; i < P5_OWNERS; ++i) CHECK(a.used[i] == b.used[i]);
}
static void done(void)
{
    CHECK(p5_end() == SQLITE_OK);
    P5Stats s = p5_stats();
    CHECK(s.shutdown_calls == 1);
    CHECK(s.live == 0 && total(s) == 0);
    CHECK(sqlite3_memory_used() == 0);
    CHECK(file_opens == 0);
}

static void hooks(int mode)
{
    CHECK(p5_begin(mode) == SQLITE_OK);
    P5Stats start = p5_stats();
    CHECK(start.init_calls == 1);
    CHECK(start.heap_bytes + start.tag_bytes + start.state_bytes == P5_BUDGET);
    CHECK(start.state_bytes > 0 && start.state_bytes % 8 == 0);
    CHECK(start.tag_bytes == (mode == P5_TABLE ? P5_TAGS * 8 : 0));
    /* Values immediately before/on/after buddy boundaries expose tag rounding. */
    const int sizes[] = {1, 24, 25, 31, 32, 33, 56, 57, 63, 64, 65, 1024, 4096};
    int measured = 0;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        int n = sizes[i];
        p5_context(P5_FEP);
        unsigned char *p = sqlite3_malloc(n);
        CHECK(p && p5_in_heap(p) && (size_t)p % 8 == 0);
        int expected = charge(mode, n);
        CHECK(sqlite3_msize(p) == (unsigned)(expected - (mode == P5_PREFIX ? P5_PREFIX_BYTES : 0)));
        CHECK(p5_stats().used[P5_FEP] == start.used[P5_FEP] + expected);
        CHECK(total(p5_stats()) - total(start) == expected);
        memset(p, 0x5a, (size_t)sqlite3_msize(p));
        p5_context(P5_APP); /* Freeing context must not change attribution. */
        sqlite3_free(p);
        same_allocations(start, p5_stats());
        measured += expected;
    }
    printf("MEASURE mode=%d heap=%d tags=%d state=%d total=%d boundary_charge=%d\n",
           mode, start.heap_bytes, start.tag_bytes, start.state_bytes, P5_BUDGET, measured);
    done();
}

static void realloc_case(int mode)
{
    CHECK(p5_begin(mode) == SQLITE_OK);
    P5Stats start = p5_stats();
    p5_context(P5_FEP);
    unsigned char *p = sqlite3_malloc(65);
    CHECK(p);
    int size = (int)sqlite3_msize(p);
    memset(p, 0xa7, size);
    P5Stats before = p5_stats();
    sqlite3_int64 sqlite_before = sqlite3_memory_used();
    /* Rounded to 1MiB: real MEMSYS5 cannot satisfy this within its heap. */
    p5_context(P5_APP);
    CHECK(sqlite3_realloc(p, P5_BUDGET) == 0);
    CHECK(p5_stats().realloc_calls > before.realloc_calls);
    same_allocations(before, p5_stats());
    CHECK(sqlite3_memory_used() == sqlite_before);
    CHECK(sqlite3_msize(p) == (unsigned)size);
    for (int i = 0; i < size; ++i) CHECK(p[i] == 0xa7);
    /* Successful moving realloc, followed by MEMSYS5's non-shrinking realloc. */
    unsigned char *next = sqlite3_realloc(p, 4096);
    CHECK(next && next != p && p5_in_heap(next));
    for (int i = 0; i < size; ++i) CHECK(next[i] == 0xa7);
    CHECK(p5_stats().used[P5_FEP] - start.used[P5_FEP] == charge(mode, 4096));
    CHECK(p5_stats().used[P5_APP] == start.used[P5_APP]);
    CHECK(sqlite3_realloc(next, 16) == next);
    CHECK(p5_stats().used[P5_FEP] - start.used[P5_FEP] == charge(mode, 4096));
    sqlite3_free(next);
    same_allocations(start, p5_stats());
    CHECK(sqlite3_realloc(0, 0) == 0);
    p5_context(P5_APP);
    p = sqlite3_realloc(0, 7);
    CHECK(p);
    p5_context(P5_SYSTEM);
    CHECK(sqlite3_realloc(p, 0) == 0);
    same_allocations(start, p5_stats());
    printf("MEASURE mode=%d realloc_failed_old=%d realloc_grown_charge=%d\n",
           mode, size, charge(mode, 4096));
    done();
}

static void capacity(void)
{
    CHECK(p5_begin(P5_TABLE) == SQLITE_OK);
    P5Stats start = p5_stats();
    void *blocks[P5_TAGS]; /* Observer storage, never used by the wrapper. */
    for (int i = 0; i < P5_TAGS; ++i) {
        p5_context(i % P5_OWNERS);
        blocks[i] = sqlite3_malloc(1);
        CHECK(blocks[i]);
    }
    P5Stats full = p5_stats();
    CHECK(full.live == P5_TAGS);
    CHECK(total(full) - total(start) == 32 * P5_TAGS);
    CHECK(sqlite3_malloc(1) == 0);
    P5Stats rejected = p5_stats();
    same_allocations(full, rejected);
    CHECK(rejected.malloc_calls == full.malloc_calls);
    CHECK(rejected.table_denials == full.table_denials + 1);
    /* Realloc uses its original tag and owner even at full capacity. */
    p5_context(P5_APP);
    blocks[0] = sqlite3_realloc(blocks[0], 4096);
    CHECK(blocks[0]);
    CHECK(p5_stats().used[P5_SYSTEM] == full.used[P5_SYSTEM] + 4096 - 32);
    for (int i = P5_TAGS - 1; i >= 0; --i) {
        p5_context((i + 1) % P5_OWNERS);
        sqlite3_free(blocks[i]);
    }
    same_allocations(start, p5_stats());
    for (int i = 0; i < P5_TAGS; ++i) {
        blocks[i] = sqlite3_malloc(1);
        CHECK(blocks[i]);
    }
    for (int i = 0; i < P5_TAGS; ++i) sqlite3_free(blocks[i]);
    same_allocations(start, p5_stats());
    printf("MEASURE mode=1 table_full=%d table_charge=%d denial_raw_calls=%d reuse=%d\n",
           full.live, total(full), rejected.malloc_calls - full.malloc_calls, P5_TAGS);
    done();
}

static void exhaust(int mode)
{
    CHECK(p5_begin(mode) == SQLITE_OK);
    P5Stats start = p5_stats();
    void *blocks[P5_TAGS];
    int n = 0;
    p5_context(P5_APP);
    while (n < P5_TAGS && (blocks[n] = sqlite3_malloc(1024)) != 0) n++;
    CHECK(n > 0 && n < P5_TAGS);
    P5Stats full = p5_stats();
    CHECK(full.table_denials == 0);
    CHECK(total(full) - total(start) == n * charge(mode, 1024));
    CHECK(total(full) + full.tag_bytes + full.state_bytes <= P5_BUDGET);
    p5_context(P5_FEP);
    while (n) sqlite3_free(blocks[--n]);
    same_allocations(start, p5_stats());
    printf("MEASURE mode=%d exhausted_live=%d exhausted_charge=%d heap_control_slack=%d\n",
           mode, full.live, total(full), full.heap_bytes - total(full));
    done();
}

static void sql(int mode)
{
    CHECK(p5_begin(mode) == SQLITE_OK);
    P5Stats start = p5_stats();
    /* Shared engine scratch is SYSTEM; connection allocations are FEP/APP. */
    void *shared = sqlite3_malloc(64);
    CHECK(shared);
    sqlite3 *db = 0;
    p5_context(P5_FEP);
    CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE dict(yomi TEXT, kanji TEXT);"
                      "INSERT INTO dict VALUES('かな', '仮名');", 0, 0, 0) == SQLITE_OK);
    p5_context(P5_APP);
    sqlite3_stmt *stmt = 0;
    CHECK(sqlite3_prepare_v2(db, "SELECT kanji FROM dict WHERE yomi=?", -1, &stmt, 0) == SQLITE_OK);
    CHECK(sqlite3_bind_text(stmt, 1, "かな", -1, SQLITE_STATIC) == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(strcmp((const char *)sqlite3_column_text(stmt, 0), "仮名") == 0);
    P5Stats s = p5_stats();
    CHECK(s.used[P5_SYSTEM] > 0 && s.used[P5_FEP] > 0 && s.used[P5_APP] > 0);
    CHECK(total(s) + s.tag_bytes + s.state_bytes <= P5_BUDGET);
    p5_context(P5_SYSTEM);
    CHECK(sqlite3_finalize(stmt) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    p5_context(P5_FEP);
    sqlite3_free(shared);
    same_allocations(start, p5_stats());
    printf("MEASURE mode=%d sql_system=%d sql_fep=%d sql_app=%d peak=%d peak_live=%d\n",
           mode, s.used[0], s.used[1], s.used[2], s.peak_bytes, s.peak_live);
    done();
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    int mode = atoi(argv[2]);
    CHECK(mode == P5_PREFIX || mode == P5_TABLE);
    if (!strcmp(argv[1], "hooks")) hooks(mode);
    else if (!strcmp(argv[1], "realloc")) realloc_case(mode);
    else if (!strcmp(argv[1], "capacity")) capacity();
    else if (!strcmp(argv[1], "exhaust")) exhaust(mode);
    else if (!strcmp(argv[1], "sql")) sql(mode);
    else CHECK(0);
    puts("PASS real MEMSYS5 hooks/accounting; remaining=0");
    return 0;
}
