/* Actual relocated SQLite body, full resident adapter and NASM stack switch. */
#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "module_runtime.h"
#include "module.h"
#include "memory_boot.h"
#include "pgalloc.h"
#include "paging.h"
#include "memmap.h"
#include "bootinfo.h"
#include "config.h"
#include "ime.h"
#include "kmalloc.h"
#include "unicode_file.h"
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
u32 paging_sqlite_stack_guard;
static int guard_np, guard_restored;
static void stack_usage(void);
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
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void number(u32 value)
{
    char digits[11];
    u32 n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) { char text[2] = {digits[--n], 0}; say(text); }
}
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n"); finish(1); } } while (0)
/* Real FEP buffer allocation/abort and dictionary close. Disk dictionary open
 * is replaced with real SQLite RAM statements; VFS disk/IME SQL are covered by
 * ime_dict_host. This does not emulate hardware faults or boot timings. */
static IME_State g_ime;
static u16 *g_cand_save_text;
static u8 *g_cand_save_attr;
static u8 fep_buffers[2][512];
static int fep_allocs, fep_frees, learn_unavailable, reserve_clamped;
typedef struct {
    sqlite3_mem_methods backing;
    unsigned context, live[3], reserve;
} HostBudgetState;
static u8 unicode_storage[UNICODE_FILE_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u32 unicode_pages;
volatile u32 tick_count;
void *kmalloc(u32 n)
{
    CHECK(!services_public && n <= sizeof(fep_buffers[0]) && fep_allocs < 2);
    return fep_buffers[fep_allocs++];
}
u32 kmalloc_block_size(void *p)
{ return p == fep_buffers[0] ? 480 : 240; }
void kfree(void *p)
{
    CHECK(p == fep_buffers[0] || p == fep_buffers[1]);
    CHECK(sqlite_module_ready());
    fep_frees++;
}
int ime_dict_open(IME_Dict *d, const char *path)
{
    (void)path;
    CHECK(!services_public && services_stage == SERVICES_SETTINGS_CLOSED);
    CHECK(os32_sqlite_outer_boot_check(0, 0) == SQLITE_OK);
    CHECK(!os32_sqlite_outer_group_acquire(OS32_SQLITE_RESIDENT, 0, &d->group));
    sqlite3 *db = 0;
    CHECK(!os32_sqlite_outer_group_open(&d->group, ":memory:", SQLITE_OPEN_READWRITE, &db));
    d->db = db;
    sqlite3_stmt *stmt = 0;
    CHECK(!os32_sqlite_prepare_v2(db, "SELECT 42", -1, &stmt, 0));
    d->exact_stmt = stmt;
    if (input[0] == 18) return -1; /* live group + partial prepared statement */
    CHECK(!os32_sqlite_prepare_v2(db, "SELECT 43", -1, &stmt, 0));
    d->prefix_stmt = stmt;
    if (input[0] == 32) {
        /* Real prepare failure at the learning boundary; search statements live.
         * Disk dictionary open is still the explicit RAM fixture above. */
        stmt = 0;
        CHECK(os32_sqlite_prepare_v2(db, "SELECT FROM", -1, &stmt, 0) == SQLITE_ERROR);
        CHECK(!stmt);
        d->learn_stmt = 0;
        return 0;
    }
    CHECK(!os32_sqlite_prepare_v2(db, "SELECT 44", -1, &stmt, 0));
    d->learn_stmt = stmt;
    return 0;
}
#include "services_fep.inc"
int vfs_stat(const char *path, OS32_Stat *st)
{
    CHECK(!memcmp(path, SYS_UNICODE_BIN, sizeof(SYS_UNICODE_BIN)));
    if (input[0] == 14) return VFS_ERR_NOTFOUND;
    st->st_size = input[0] == 15 ? 128 * 1024 : UNICODE_FILE_BYTES;
    return 0;
}
int vfs_read(const char *path, void *data, u32 size)
{
    (void)path;
    CHECK(size == UNICODE_FILE_BYTES);
    if (input[0] == 16) return -1;
    u8 *out = data;
    for (u32 i = 0; i < UNICODE_FILE_PAIRS; i++) {
        out[i*4] = i; out[i*4+1] = i >> 8;
        out[i*4+2] = 0x21; out[i*4+3] = 0x24;
    }
    return size;
}

static void protect(u32 start, int prot)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(start),
                     "c"(PAGE_SIZE), "d"(prot) : "memory");
    CHECK(!rc);
}
int paging_set_not_present(u32 start, u32 end)
{
    CHECK(start == stack_base - PAGE_SIZE && end == stack_base - 1 && !guard_np);
    protect(start, 0);
    guard_np = 1;
    return 0;
}
int paging_is_present(u32 va)
{
    CHECK(va == paging_sqlite_stack_guard);
    return !guard_np;
}
int paging_set_page(u32 va, u32 pa, u32 flags)
{
    CHECK(va == stack_base - PAGE_SIZE && pa == va && flags == PAGE_RW);
    if (input[0] == 0) stack_usage(); /* Includes the final engine shutdown. */
    protect(va, 7);
    guard_np = 0; guard_restored++;
    return 0;
}
/* An overflowing ESP faults onto a host signal stack, then takes the actual
 * kernel fail-stop diagnostic. It cannot return to the overflowing engine. */
static u8 signal_stack[16384];
void host_stop(void) { CHECK(sqlite_engine_active); finish(77); }
static void sputs(const char *s) { (void)s; }
static void sput_hex32(u32 value) { (void)value; }
static void tvram_puts_at(int r, int c, const char *s, u8 a)
{ (void)r; (void)c; (void)s; (void)a; }
static void tvram_put_hex32(int r, int c, u32 value, u8 a)
{ (void)r; (void)c; (void)value; (void)a; }
#include "sqlite_fault.inc"
static void stack_fault(int signal)
{
    CHECK(signal == 11 && sqlite_engine_active);
    say("PASS guard overflow fault -> engine fail-stop\n");
    sqlite_engine_fault(14, 0, stack_base - sizeof(u32));
    CHECK(0);
}
static void overflow(void *arg)
{
    (void)arg;
    u32 saved;
    __asm__ volatile("mov %%esp, %0; mov %1, %%esp; pushl $0; mov %0, %%esp"
                     : "=&r"(saved) : "r"(stack_base) : "memory");
    CHECK(0);
}
static void overflow_test(void)
{
    struct { void *sp; int flags; u32 size; } alt = {signal_stack, 0, sizeof(signal_stack)};
    struct { void (*handler)(int); u32 flags; void *restorer; u32 mask[2]; }
        action = {stack_fault, 0x08000000u, 0, {0, 0}}; /* SA_ONSTACK */
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(186), "b"(&alt), "c"(0) : "memory");
    CHECK(!rc);
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(174), "b"(11), "c"(&action),
                     "d"(0), "S"(8) : "memory");
    CHECK(!rc);
    sqlite_engine_run(overflow, 0, 0);
    CHECK(0);
}
static void stack_usage(void)
{
    u8 *base = (u8 *)(uptr)stack_base;
    u32 untouched = 0;
    while (untouched < SQLITE_ENGINE_STACK_SIZE && base[untouched] == 0xcc) untouched++;
    u32 used = SQLITE_ENGINE_STACK_SIZE - untouched;
    char digits[11];
    u32 n = 0;
    do { digits[n++] = '0' + used % 10; used /= 10; } while (used);
    say("STACK high-water init/CRUD/diagnostics/shutdown: ");
    while (n) { char text[2] = {digits[--n], 0}; say(text); }
    say(" / 131072 bytes\n");
}
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
int vfs_quarantine_sqlite(const VfsSqliteCookie *cookie)
{
    CHECK(cookie && cookie->generation);
    stack_probe();
    return 0;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (input[0] >= 10 && services_stage != SERVICES_READY)
        CHECK(!services_public);
    if (!memcmp(fmt, "IME: 学習不可", sizeof("IME: 学習不可") - 1)) learn_unavailable++;
    if (!memcmp(fmt, "[services] fep_mem_reserve_kb below default", sizeof("[services] fep_mem_reserve_kb below default") - 1)) reserve_clamped++;
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
    return input[0] == 1 || input[0] == 11 ? -1 : 3;
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
    if (owner == LEDGER_OWNER_KERNEL) {
        CHECK(n == UNICODE_FILE_PAGES && !unicode_pages);
        if (input[0] == 17) return 0;
        unicode_pages = n;
        return (u32)(uptr)unicode_storage;
    }
    CHECK(owner == OWNER && owned && n > 0);
    alloc_calls++;
    if (input[0] == 13) return 0;
    if ((input[0] == 4 && alloc_calls == 2) || (input[0] == 5 && alloc_calls == 3)) return 0;
    CHECK(allocated + n <= POOL_PAGES);
    u32 address = (u32)(uptr)(pool + allocated * PAGE_SIZE);
    for (int i = 0; i < n; i++) live[allocated + i] = 1;
    allocated += n;
    kmemset((void *)(uptr)address, 0xcc, n * PAGE_SIZE);
    if (alloc_calls == 3) {
        CHECK(n * PAGE_SIZE == SQLITE_ENGINE_STACK_SIZE + PAGE_SIZE);
        stack_base = address + PAGE_SIZE;
    }
    if (alloc_calls == 2) {
        CHECK(n * PAGE_SIZE == SQLITE_MODULE_POOL_SIZE);
        heap_base = address;
    }
    return address;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    if (owner == LEDGER_OWNER_KERNEL) {
        CHECK(pfn == (u32)(uptr)unicode_storage / PAGE_SIZE && n == UNICODE_FILE_PAGES);
        unicode_pages = 0;
        return 1;
    }
    CHECK(owner == OWNER && n > 0);
    u32 first = pfn - (u32)(uptr)pool / PAGE_SIZE;
    CHECK(first + n <= allocated);
    if (stack_base && first <= (stack_base - PAGE_SIZE - (u32)(uptr)pool) / PAGE_SIZE &&
        first + n > (stack_base - PAGE_SIZE - (u32)(uptr)pool) / PAGE_SIZE)
        CHECK(!guard_np && guard_restored == 1 && !paging_sqlite_stack_guard);
    if (input[0] == 3 && !prefix_failed) { prefix_failed = 1; return 0; }
    for (int i = 0; i < n; i++) { CHECK(live[first + i]); live[first + i] = 0; }
    kmemset(pool + first * PAGE_SIZE, 0xdd, n * PAGE_SIZE);
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    if (owner == LEDGER_OWNER_KERNEL) { *pages = unicode_pages; unicode_pages = 0; return 1; }
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
    if (owner == LEDGER_OWNER_KERNEL) return unicode_pages;
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
void host_seed_settings(sqlite3 *db)
{
    if (input[0] != 19 && input[0] != 20) return;
    CHECK(!os32_sqlite_exec(db, "CREATE TABLE settings(scope TEXT,key TEXT,type INTEGER,ival INTEGER)", 0, 0, 0));
    CHECK(!os32_sqlite_exec(db, input[0] == 19 ?
        "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,224)" :
        "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,128)", 0, 0, 0));
}
static void boot_transaction(void)
{
    services_stage = SERVICES_ROOT_READY;
    unicode_file_load();
    services_stage = SERVICES_UNICODE;
    if (input[0] > 20 && input[0] <= 31) services_boot_fault = input[0] - 20;
    u32 unicode_before = unicode_pages;
    services_boot();
    CHECK(unicode_pages == unicode_before);
    CHECK(unicode_before == ((input[0] >= 14 && input[0] <= 17) ? 0 : UNICODE_FILE_PAGES));
    CHECK(unicode_file_lookup(0x3041) == 0x2421);
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    int ok = input[0] == 10 || input[0] == 19 || input[0] == 20 || input[0] == 32 || input[0] == 33 || (input[0] >= 14 && input[0] <= 17);
    int isolated = services_boot_fault == SERVICES_FAULT_SETTINGS_CLOSE ||
                   services_boot_fault == SERVICES_FAULT_FEP_CLOSE ||
                   services_boot_fault == SERVICES_FAULT_SHUTDOWN;
    CHECK(services_public == ok);
    if (ok) {
        CHECK(services_stage == SERVICES_READY && sqlite_module_ready());
        CHECK(learn_unavailable == (input[0] == 32));
        CHECK((g_ime.dict.learn_stmt == 0) == (input[0] == 32));
        CHECK(reserve_clamped == (input[0] == 20));
        HostBudgetState *budget = (HostBudgetState *)(uptr)heap_base;
        CHECK(budget->reserve == (input[0] == 19 ? 224 * 1024 : OS32_SQLITE_FEP_RESERVE));
        CHECK(g_ime.dict_loaded && fep_allocs == 2 && !fep_frees);
        /* Real APP pressure must leave the reservation available to FEP. */
        Os32SqliteGroup group;
        sqlite3 *db = 0;
        CHECK(!os32_sqlite_outer_group_acquire(OS32_SQLITE_EXEC, 1, &group));
        CHECK(!os32_sqlite_outer_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &db));
        CHECK(!os32_sqlite_exec(db, "CREATE TABLE pressure(x)", 0, 0, 0));
        int inserts = 0;
        while (os32_sqlite_exec(db, "INSERT INTO pressure VALUES(zeroblob(4096))", 0, 0, 0) == SQLITE_OK) {
            CHECK(++inserts < 200);
        }
        CHECK(inserts > 0);
        CHECK(budget->live[0] + budget->live[2] <= 319488u -
              (budget->reserve - OS32_SQLITE_FEP_RESERVE));
        CHECK(os32_sqlite_step(g_ime.dict.exact_stmt) == SQLITE_ROW);
        CHECK(os32_sqlite_column_int(g_ime.dict.exact_stmt, 0) == 42);
        CHECK(!os32_sqlite_reset(g_ime.dict.exact_stmt));
        CHECK(!os32_sqlite_outer_group_begin_close(&group));
        CHECK(!os32_sqlite_close(db));
        CHECK(!os32_sqlite_outer_group_finish(&group, SQLITE_OK));
        if (input[0] == 20) {
            CHECK(os32_sqlite_outer_boot_check(1, 128 * 1024) == SQLITE_OK);
            CHECK(budget->reserve == OS32_SQLITE_FEP_RESERVE);
        }
        say("SERVICES_READY sqlite>0 fep=736 quarantine=0 unicode retained\n");
    } else if (isolated) {
        CHECK(services_stage == SERVICES_MINIMAL_QUARANTINED);
        CHECK(module_records[0].state == MODULE_QUARANTINED);
        CHECK(owned && ledger_owner_pages(OWNER) > 0 && !retired && guard_np);
        CHECK((services_boot_fault == SERVICES_FAULT_FEP_CLOSE) == (ime_boot_bytes() != 0));
        stubs();
        say("MINIMAL_QUARANTINED sqlite>0 group/heap/stack retained unicode retained\n");
    } else {
        CHECK(services_stage == SERVICES_MINIMAL);
        CHECK(module_records[0].state == MODULE_ABSENT);
        CHECK(!owned && !ledger_owner_pages(OWNER) && !ime_boot_bytes());
        CHECK(fep_allocs == fep_frees && !guard_np);
        stubs();
        say("MINIMAL sqlite=0 fep=0 quarantine=0 unicode retained\n");
    }
    u32 pages = ledger_owner_pages(OWNER);
    u32 expected_pages = (((const u32 *)file)[MI_MEM_SIZE] + SQLITE_MODULE_POOL_SIZE +
                          SQLITE_ENGINE_STACK_SIZE + PAGE_SIZE) / PAGE_SIZE;
    CHECK(pages == ((ok || isolated) ? expected_pages : 0));
    say("occupancy sqlite_pages="); number(pages);
    say(" fep_bytes="); number(ime_boot_bytes());
    say(" quarantine_bytes="); number(isolated ? pages * PAGE_SIZE + ime_boot_bytes() : 0);
    say(" unicode_pages="); number(unicode_pages); say("\n");
    u32 before = alloc_calls;
    services_boot();
    CHECK(alloc_calls == before); /* boot transaction is single-shot */
    if (input[0] == 33) {
        services_boot_fault = SERVICES_FAULT_FEP_CLOSE;
        CHECK(ime_dict_close(&g_ime.dict) == 0);
        CHECK(!g_ime.dict.isolated && !g_ime.dict.db);
        CHECK(os32_sqlite_outer_boot_check(0, 0) == SQLITE_OK);
        say("PASS boot close fault ignored after SERVICES_READY\n");
    }
    finish(0);
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
    if (input[0] >= 10) boot_transaction();
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
    CHECK(guard_np && paging_sqlite_stack_guard == stack_base - PAGE_SIZE);
    if (input[0] == 6) overflow_test();
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
    /* Corrupt the word immediately before the MEMSYS5 heap: accounting
     * state must precede the canary, never separate it from the heap. */
    u32 state_bytes = (sizeof(HostBudgetState) + 7u) & ~7u;
    u32 *first = (u32 *)(uptr)(heap_base + state_bytes + 4 * sizeof(u32) - sizeof(u32));
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
