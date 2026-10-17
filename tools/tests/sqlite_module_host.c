/* Actual relocated SQLite body, full resident adapter and NASM stack switch. */
#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "module_runtime.h"
#include "module.h"
#include "memory_boot.h"
#include "pgalloc.h"
#include "memmap.h"
#include "bootinfo.h"
#include "config.h"
#include "kstring.h"
#include "kprintf.h"
#include "lib/sqlite3/generated/sqlite_facade.h"
#include <stdarg.h>

#define IMAGE_CAP (544768U)
#define POOL_PAGES 512U
#define OWNER (LEDGER_OWNER_FIXED_LAST + 1)
static u8 file[IMAGE_CAP];
static u8 pool[POOL_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u8 live[POOL_PAGES];
static u32 input[4]; /* mode, image size, pair, expected result */
static u32 pos, opens, closes, owned, retired, allocated, alloc_calls;
static u32 stack_base, heap_base, prefix_failed, init_calls, stack_probes;
static struct bootinfo bi;
u32 host_flags = 0x202;
volatile u32 kctx_irq_depth, kctx_exc_depth;
struct kdebug kdebug;

static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n"); finish(1); } } while (0)
void *kmemcpy(void *d, const void *s, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
void *kmemset(void *d, int c, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = c;
    return d;
}
int memcmp(const void *a, const void *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (((const u8 *)a)[i] != ((const u8 *)b)[i]) return 1;
    return 0;
}
static void read_input(void *p, u32 n)
{
    while (n) {
        int rc;
        __asm__ volatile("int $0x80" : "=a"(rc) : "0"(3), "b"(0), "c"(p), "d"(n) : "memory");
        CHECK(rc > 0);
        n -= rc; p = (u8 *)p + rc;
    }
}
static void stack_probe(void)
{
    u8 local;
    CHECK(sqlite_engine_active && stack_base);
    CHECK((u32)(uptr)&local >= stack_base);
    CHECK((u32)(uptr)&local < stack_base + SQLITE_ENGINE_STACK_SIZE);
    CHECK(sqlite_engine_stack_top == stack_base + SQLITE_ENGINE_STACK_SIZE);
    stack_probes++;
}
void sqlite_forbidden_import(void) { say("FAIL unexpected SQLite import\n"); finish(1); }
u32 sys_time(void) { stack_probe(); return 1700000000; }
u32 ktime_ticks(void) { stack_probe(); return 12345; }
int vfs_count_sqlite(const VfsSqliteCookie *cookie)
{
    CHECK(cookie && cookie->generation);
    stack_probe(); return 0;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (!sqlite_engine_active) return;
    stack_probe();
    if (!memcmp(fmt, "[SQT] init:", 11)) {
        CHECK(!sqlite_module_ready() && sqlite_module_api == &sqlite_stub_api);
        init_calls++;
        va_list args;
        va_start(args, fmt);
        u32 pa = va_arg(args, unsigned), size = va_arg(args, unsigned);
        CHECK(size == SQLITE_MODULE_POOL_SIZE);
        CHECK(pa >= (u32)(uptr)pool && pa + size <= (u32)(uptr)(pool + sizeof(pool)));
        va_end(args);
    }
}
const struct bootinfo *bootinfo_get(void) { return &bi; }
int vfs_open(const char *path, int mode)
{
    CHECK(!memcmp(path, SYS_SQLITE_MOD, sizeof(SYS_SQLITE_MOD)) && mode == O_RDONLY);
    opens++;
    return input[0] == 1 ? -1 : 3;
}
void vfs_close(int fd) { CHECK(fd == 3 && !closes); closes++; }
int vfs_fstat(int fd, OS32_Stat *st)
{
    CHECK(fd == 3); st->st_size = input[1]; return 0;
}
int vfs_read_fd(int fd, void *dst, u32 n)
{
    CHECK(fd == 3 && !closes && n <= input[1] - pos);
    kmemcpy(dst, file + pos, n); pos += n; return n;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out)
{
    CHECK(!owned && kind == LEDGER_KIND_MODULE && id == MODULE_ID_SQLITE && name[0] == 's');
    owned = 1; *out = OWNER; return 1;
}
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    CHECK(owner == OWNER && owned && n > 0);
    alloc_calls++;
    if ((input[0] == 4 && alloc_calls == 2) || (input[0] == 5 && alloc_calls == 3)) return 0;
    CHECK(allocated + n <= POOL_PAGES);
    u32 address = (u32)(uptr)(pool + allocated * PAGE_SIZE);
    for (int i = 0; i < n; i++) live[allocated + i] = 1;
    allocated += n;
    kmemset((void *)(uptr)address, 0xcc, n * PAGE_SIZE);
    if (alloc_calls == 3) {
        CHECK(n * PAGE_SIZE == SQLITE_ENGINE_STACK_SIZE);
        stack_base = address;
    }
    if (alloc_calls == 2) {
        CHECK(n * PAGE_SIZE == SQLITE_MODULE_POOL_SIZE);
        heap_base = address;
    }
    return address;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    CHECK(owner == OWNER && n > 0);
    u32 first = pfn - (u32)(uptr)pool / PAGE_SIZE;
    CHECK(first + n <= allocated);
    if (input[0] == 3 && !prefix_failed) { prefix_failed = 1; return 0; }
    for (int i = 0; i < n; i++) { CHECK(live[first + i]); live[first + i] = 0; }
    kmemset(pool + first * PAGE_SIZE, 0xdd, n * PAGE_SIZE);
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    CHECK(owner == OWNER && !sqlite_engine_active);
    *pages = 0;
    for (u32 i = 0; i < allocated; i++) if (live[i]) {
        CHECK(pgalloc_free_n_owner(owner, (u32)(uptr)pool / PAGE_SIZE + i, 1));
        ++*pages;
    }
    return 1;
}
u32 ledger_owner_pages(u32 owner)
{
    CHECK(owner == OWNER);
    u32 count = 0;
    for (u32 i = 0; i < allocated; i++) count += live[i];
    return count;
}
int ledger_owner_retire(u32 owner)
{
    CHECK(owner == OWNER && !ledger_owner_pages(owner));
    retired++; owned = 0; return 1;
}
void memory_boot_page_counts(u32 first, u32 pages, struct memory_page_counts *out)
{
    CHECK(!first && !pages);
    kmemset(out, 0, sizeof(*out));
}
static void stubs(void)
{
    CHECK(!sqlite_module_ready() && sqlite_module_api == &sqlite_stub_api);
    const u32 *entries = (const u32 *)&sqlite_stub_api;
    for (u32 i = 0; i < SQLITE_MODULE_API_SLOTS; i++) CHECK(entries[i]);
    CHECK(os32_sqlite_close(0) == OS32_ERR_NOSYS);
    CHECK(os32_sqlite_exec(0, "select 1", 0, 0, 0) == OS32_ERR_NOSYS);
    CHECK(os32_sqlite_memory_used() == 0);
    CHECK(os32_sqlite_outer_canary() == -1);
}
static int selected(void *arg, int n, char **values, char **names)
{
    (void)names;
    stack_probe();
    CHECK(n == 1 && !memcmp(values[0], "42", 3));
    ++*(int *)arg;
    return 0;
}
void _start(void)
{
    read_input(input, sizeof(input)); CHECK(input[1] <= IMAGE_CAP);
    read_input(file, input[1]);
    bi.img_valid = 1; bi.img_crc = input[2];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(pool), "c"(sizeof(pool)), "d"(7) : "memory");
    CHECK(!rc);
    stubs();
    rc = os32_sqlite_start();
    CHECK(rc == (int)input[3] && opens == 1 && closes == (input[0] != 1));
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    struct module_handle handle = {0, module_records[0].generation};
    if (rc) {
        CHECK(!sqlite_engine_stack_top && !owned && retired == (alloc_calls != 0));
        CHECK(!ledger_owner_pages(OWNER) && module_records[0].state == MODULE_ABSENT);
        CHECK(init_calls == (input[0] == 3));
        stubs(); finish(0);
    }
    CHECK(sqlite_module_ready() && init_calls == 1 && stack_probes >= 3);
    CHECK(owned && !retired && alloc_calls == 3 && module_exports(handle));
    struct module_record *r = module_record_get(handle);
    CHECK(r->state == MODULE_RUNNING && r->prefix_pages == 0);
    const u32 *entries = (const u32 *)sqlite_module_api;
    for (u32 i = 0; i < SQLITE_MODULE_API_SLOTS; i++)
        CHECK(entries[i] >= r->text && entries[i] - r->text < r->text_size);
    u32 prefix = ((const u32 *)file)[MI_PAYLOAD_OFF];
    for (u32 i = 0; i < prefix; i++) CHECK(pool[i] == 0xdd);
    /* Real relocated SQLite executes after its prefix was destroyed. */
    Os32SqliteGroup group;
    sqlite3 *db = 0;
    CHECK(os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &group) == SQLITE_OK);
    CHECK(os32_sqlite_outer_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &db) == SQLITE_OK);
    CHECK(db && os32_sqlite_memory_used() > 0);
    CHECK(os32_sqlite_exec(db, "CREATE TABLE t(v INTEGER); INSERT INTO t VALUES(41); UPDATE t SET v=v+1;",
                           0, 0, 0) == SQLITE_OK);
    int rows = 0;
    CHECK(os32_sqlite_exec(db, "SELECT v FROM t", selected, &rows, 0) == SQLITE_OK && rows == 1);
    sqlite3_stmt *stmt = 0;
    CHECK(os32_sqlite_prepare_v2(db, "SELECT v FROM t", -1, &stmt, 0) == SQLITE_OK);
    CHECK(os32_sqlite_step(stmt) == SQLITE_ROW && os32_sqlite_column_int(stmt, 0) == 42);
    CHECK(os32_sqlite_step(stmt) == SQLITE_DONE);
    CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
    CHECK(os32_sqlite_exec(db, "DELETE FROM t", 0, 0, 0) == SQLITE_OK);
    CHECK(os32_sqlite_outer_canary() == 0);
    /* Sentinels must surround the actual owner pool, not module data. */
    u32 *first = (u32 *)(uptr)heap_base;
    u32 *last = (u32 *)(uptr)(heap_base + SQLITE_MODULE_POOL_SIZE - sizeof(u32));
    u32 saved = *first;
    *first ^= 1;
    CHECK(os32_sqlite_outer_canary() == -1);
    *first = saved;
    CHECK(os32_sqlite_outer_canary() == 0);
    saved = *last;
    *last ^= 1;
    CHECK(os32_sqlite_outer_canary() == -2);
    *last = saved;
    CHECK(os32_sqlite_outer_canary() == 0);
    CHECK(os32_sqlite_outer_group_begin_close(&group) == SQLITE_OK);
    CHECK(os32_sqlite_close(db) == SQLITE_OK);
    CHECK(os32_sqlite_outer_group_finish(&group, SQLITE_OK) == SQLITE_OK);
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    CHECK(!module_abort(handle) && !owned && retired == 1 && !sqlite_engine_stack_top);
    stubs();
    say("PASS real SQLite init/CRUD/typed diagnostics/prefix reuse/128KiB stack/reclaim\n");
    finish(0);
}
