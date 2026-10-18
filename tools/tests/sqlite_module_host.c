/* Actual relocated SQLite body, full resident adapter and NASM stack switch. */
#define OS32_SQLITE_OUTER_IMPL
#include "sqlite_engine.h"
#include "lib/sqlite3/os32_sqlite_config.h"
#include "module_runtime.h"
#include "module.h"
#include "memory_boot.h"
#include "pgalloc.h"
#include "paging.h"
#include "memmap.h"
#include "bootinfo.h"
#include "config.h"
#include "ime.h"
#include "fep_module.h"
#include "fep_host_offsets.h"
#include "kbd.h"
#include "kmalloc.h"
#include "unicode_file.h"
#include "kstring.h"
#include "kprintf.h"
#include "lib/sqlite3/generated/sqlite_facade.h"
#include <stdarg.h>

#define IMAGE_CAP (544768U)
#define POOL_PAGES 512U
#define OWNER (LEDGER_OWNER_FIXED_LAST + 1)
static u8 file[IMAGE_CAP], fep_file[IMAGE_CAP];
static u32 fep_size, fep_pos, fep_opens, fep_closes, fep_owned, fep_retired;
static u8 pool[POOL_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u8 live[POOL_PAGES];
static u32 page_owner[POOL_PAGES];
static u32 input[4]; /* mode, image size, pair, expected result */
static u32 pos, opens, closes, owned, retired, allocated, alloc_calls;
static u32 stack_base, heap_base, prefix_failed, init_calls, stack_probes;
u32 paging_sqlite_stack_guard;
static int guard_np, guard_restored;
static u32 stack_reports;
static u32 dictionary_stats, dictionary_missing_messages, services_reports;
static u32 reused_owner_pages;
static void stack_usage(void);
/* Copyout seam for the real ver diagnostic entry. */
char *kstrncpy(char *d, const char *s, u32 n)
{
    u32 i = 0;
    if (!n) return d;
    while (i + 1 < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d;
}
#include "sys_build_info.inc"
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
/* Both executable bodies are the paired product images. Only dictionary
 * storage is a RAM DB seeded below; actual FEP SQL/init/close stay in fep.mod.
 * Disk/journal lifetime remains covered by ime_dict_host and VFS fixtures. */
#define g_ime (*(IME_State *)(uptr)(module_records[1].base + HOST_FEP_STATE))
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
int host_fep_group_acquire(int kind, int owner, Os32SqliteGroup *out)
{
    CHECK(kind == OS32_SQLITE_RESIDENT && owner == 0);
    return os32_sqlite_outer_group_acquire(kind, owner, out);
}
int host_fep_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **out)
{
    CHECK(!services_public && services_stage == SERVICES_SETTINGS_CLOSED);
    CHECK(!memcmp(path, "/db/fep.db", sizeof("/db/fep.db")));
    int rc = os32_sqlite_outer_group_open(group, ":memory:", flags, out);
    if (rc) return rc;
    CHECK(!os32_sqlite_exec(*out,
        "CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INTEGER,cost INTEGER);"
        "INSERT INTO dict VALUES('nihon','Japan',42,1)", 0, 0, 0));
    return 0;
}
int host_fep_prepare(sqlite3 *db, const char *sql, int n, sqlite3_stmt **out, const char **tail)
{
    /* Fail after exact prepare, or only learning prepare. These failures pass
     * through the real module's partial-init cleanup / learning fallback. */
    if ((input[0] == 18 && !memcmp(sql, "SELECT kanji, pos_id, cost FROM (", 31) &&
         g_ime.dict.exact_stmt) ||
        (input[0] == 32 && !memcmp(sql, "INSERT INTO dict_user", 21)))
        return os32_sqlite_prepare_v2(db, "SELECT FROM", -1, out, tail);
    return os32_sqlite_prepare_v2(db, sql, n, out, tail);
}
static int host_key = -1, host_sink, host_user, host_stop_pending, abort_checks;
static int host_next_key = -1, ready_check_failed;
void ring3_abort_check(void);
volatile u8 kbd_shift_state;
int kbd_trygetkey(void) { int key = host_key; host_key = host_next_key; host_next_key = -1; return key; }
int kbd_trygetchar(void) { int key = kbd_trygetkey(); return key < 0 ? -1 : key & 255; }
int kbd_getkey_wait(int interruptible) { if (interruptible && host_stop_pending) return -1; return kbd_trygetkey(); }
int kbd_getchar(void) { return kbd_trygetchar(); }
int kbd_getkey(void) { return kbd_trygetkey(); }
int kbd_getchar_kapi(void) { int c = kbd_getkey_wait(1); if (c < 0) ring3_abort_check(); return c < 0 ? c : c & 255; }
int kbd_getkey_kapi(void) { int c = kbd_getkey_wait(1); if (c < 0) ring3_abort_check(); return c; }
int con_sink_is_enabled(void) { return host_sink; }
int ring3_call_from_user(void) { return host_user; }
int res_owner_get(void) { return 7; }
void ring3_abort_check(void) { abort_checks++; }
void serial_puts(const char *s) { (void)s; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
void tvram_set_scroll_reserve(int n) { (void)n; }
static void input_paths(void)
{
    /* GUI dispatch feeds raw events; CUI reads the cooked queue. Each keeps
     * ordinary text/edit keys usable in both READY and MINIMAL. */
    CHECK(ime_feed_key(-1) == -1);
    CHECK(ime_feed_key(0x1e61) == 0x1e61);
    CHECK(ime_feed_key(0x4b00) == 0x4b00);
    CHECK(ime_feed_key(0x0e08) == 0x0e08);
    for (int sink = 0; sink <= 1; sink++) {
        host_sink = sink;
        host_key = -1; CHECK(ime_trygetkey() == -1);
        host_key = 0x1e61; CHECK(ime_trygetkey() == 0x1e61);
        host_key = 0x4b00; CHECK(ime_trygetkey() == 0x4b00);
        host_key = 0x0e08; CHECK(ime_trygetchar() == 8);
        host_key = -1; CHECK(ime_trygetchar() == -1);
        host_key = 0x1e61; CHECK(ime_getkey() == 0x1e61);
        host_key = 0x1e61; CHECK(ime_getchar() == (sink ? 0x1e61 : 'a'));
        host_key = 0x4b00; CHECK(ime_getkey_kapi() == 0x4b00);
        host_key = 0x1e61; CHECK(ime_getchar_kapi() == (sink ? 0x1e61 : 'a'));
        /* STOP is represented only at the keyboard wait seam, as on the
         * product path: wait returns -1, then the KAPI safe point is reached. */
        int before = abort_checks;
        host_stop_pending = 1;
        CHECK(ime_getkey_kapi() == -1);
        CHECK(ime_getchar_kapi() == -1);
        CHECK(abort_checks == before + 2);
        host_stop_pending = 0;
    }
    host_sink = 0;
    if (services_stage == SERVICES_MINIMAL || services_stage == SERVICES_SQLITE_ONLY) {
        host_key = KEY_F1 << 8;
        CHECK(ime_trygetchar() == -1);
        host_key = KEY_F1 << 8; host_next_key = 0x1e61;
        CHECK(ime_getchar() == 'a');
        host_key = KEY_F1 << 8; host_next_key = 0x1e61;
        CHECK(ime_getchar_kapi() == 'a');
    }
}
int vfs_stat(const char *path, OS32_Stat *st)
{
    if (!memcmp(path, "/db/fep.db", sizeof("/db/fep.db"))) {
        CHECK(services_stage == SERVICES_UNICODE && !opens && !fep_opens && !alloc_calls);
        dictionary_stats++;
        if (input[0] == 55 || (input[0] >= 57 && input[0] <= 60)) return VFS_ERR_NOTFOUND;
        if (input[0] == 56) return VFS_ERR_IO;
        st->st_size = 1;
        return 0;
    }
    if (!memcmp(path, SYS_SQLITE_MOD, sizeof(SYS_SQLITE_MOD)))
        return input[0] == 57 ? VFS_ERR_NOTFOUND : VFS_OK;
    if (!memcmp(path, SYS_FEP_MOD, sizeof(SYS_FEP_MOD)))
        return input[0] == 60 ? VFS_ERR_NOTFOUND : VFS_OK;
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
/* SA_ONSTACK tests only NP protection + the fail-stop function in isolation.
 * It does NOT model IDT #PF delivery on an exhausted kernel stack or #DF.
 * KSTK-DF requires the actual guest task-gate injections. */
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
    say("PASS host signal seam -> engine fail-stop (NOT IDT/#DF)\n");
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
static void install_stack_fault(void)
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
}
static void overflow_test(void)
{
    install_stack_fault();
    sqlite_engine_run(overflow, 0, 0);
    CHECK(0);
}
/* Modes 10..36 are boot transactions; 40..44 are stack workloads.
 * Each workload runs in a fresh process/module so high-water is per workload
 * including initialize and the common CRUD baseline. No mark reset while active. */
static void stack_workload(sqlite3 *db, u32 mode)
{
    sqlite3_stmt *stmt = 0;
    if (mode == 40) {
        install_stack_fault();
        /* Left-deep tree avoids the parser's independent parentheses limit.
         * A leaf is depth 1: 100 terms reach exactly SQLITE_MAX_EXPR_DEPTH. */
        char sql[2 * SQLITE_MAX_EXPR_DEPTH + 32];
        for (int depth = 1; depth <= SQLITE_MAX_EXPR_DEPTH + 1; depth++) {
            u32 n = 0;
            const char *prefix = "SELECT ";
            while (*prefix) sql[n++] = *prefix++;
            for (int i = 0; i < depth; i++) { if (i) sql[n++] = '+'; sql[n++] = 'v'; }
            const char *suffix = " FROM t";
            while (*suffix) sql[n++] = *suffix++;
            sql[n] = 0;
            int rc = os32_sqlite_prepare_v2(db, sql, -1, &stmt, 0);
            if (depth == SQLITE_MAX_EXPR_DEPTH + 1) { CHECK(rc == SQLITE_ERROR && !stmt); break; }
            CHECK(rc == SQLITE_OK);
            CHECK(os32_sqlite_step(stmt) == SQLITE_ROW && os32_sqlite_column_int(stmt, 0) == 42 * depth);
            CHECK(os32_sqlite_step(stmt) == SQLITE_DONE);
            CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
            if (depth == SQLITE_MAX_EXPR_DEPTH) { say("STACK expr-depth=100 prepare/step: "); number(sqlite_engine_stack_high_water()); say(" bytes\n"); }
        }
    } else if (mode == 41) {
        CHECK(os32_sqlite_prepare_v2(db,
            "SELECT v FROM t UNION SELECT v+1 FROM t UNION SELECT v+2 FROM t "
            "UNION SELECT v+3 FROM t UNION SELECT v+4 FROM t ORDER BY 1 DESC",
            -1, &stmt, 0) == SQLITE_OK);
        for (int value = 46; value >= 42; value--)
            CHECK(os32_sqlite_step(stmt) == SQLITE_ROW && os32_sqlite_column_int(stmt, 0) == value);
        CHECK(os32_sqlite_step(stmt) == SQLITE_DONE);
        CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
    } else if (mode == 42 || mode == 43) {
        CHECK(os32_sqlite_prepare_v2(db, "INSERT INTO t SELECT v+? FROM t", -1, &stmt, 0) == SQLITE_OK);
        for (int i = 1; i <= 512; i *= 2) {
            CHECK(os32_sqlite_bind_int(stmt, 1, i) == SQLITE_OK);
            CHECK(os32_sqlite_step(stmt) == SQLITE_DONE);
            CHECK(os32_sqlite_reset(stmt) == SQLITE_OK);
        }
        CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
        if (mode == 43) CHECK(os32_sqlite_exec(db, "CREATE INDEX ix ON t(v DESC)", 0, 0, 0) == SQLITE_OK);
        CHECK(os32_sqlite_prepare_v2(db, mode == 42 ?
            "SELECT v FROM t ORDER BY (v*17)%103, v DESC" : "SELECT v FROM t ORDER BY v DESC",
            -1, &stmt, 0) == SQLITE_OK);
        int old_key = -1, old_value = 0;
        for (int i = 0; i < 1024; i++) {
            CHECK(os32_sqlite_step(stmt) == SQLITE_ROW);
            int value = os32_sqlite_column_int(stmt, 0), key = mode == 42 ? (value*17)%103 : 0;
            CHECK(key > old_key || (key == old_key && value < old_value));
            old_key = key; old_value = value;
        }
        CHECK(os32_sqlite_step(stmt) == SQLITE_DONE);
        CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
    } else {
        /* Product SQLITE_OMIT_TRIGGER: there is no trigger execution stack. */
        CHECK(os32_sqlite_prepare_v2(db,
            "CREATE TRIGGER tr AFTER INSERT ON t BEGIN UPDATE t SET v=v+1; END",
            -1, &stmt, 0) == SQLITE_ERROR && !stmt);
    }
    u32 peak = sqlite_engine_stack_high_water();
    u32 required = 32 * 1024; /* depth 100 measured 24,388 B; user decision 2026-10-18 */
    say("STACK workload="); number(mode); say(" high-water="); number(peak);
    say(" required="); number(required); say(" configured="); number(SQLITE_ENGINE_STACK_SIZE); say("\n");
    CHECK(SQLITE_ENGINE_STACK_SIZE >= required);
    CHECK(os32_sqlite_outer_canary() == 0);
}

static void stack_usage(void)
{
    u32 used = sqlite_engine_stack_high_water();
    CHECK(used > 0 && used < SQLITE_ENGINE_STACK_SIZE);
    char digits[11];
    u32 n = 0;
    do { digits[n++] = '0' + used % 10; used /= 10; } while (used);
    say("STACK high-water init/CRUD/diagnostics/shutdown: ");
    while (n) { char text[2] = {digits[--n], 0}; say(text); }
    say(" / "); number(SQLITE_ENGINE_STACK_SIZE); say(" bytes\n");
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
    if (!memcmp(fmt, "[services] SQLITE_ONLY: no FEP dictionary\n",
                sizeof("[services] SQLITE_ONLY: no FEP dictionary\n")))
        dictionary_missing_messages++;
    if (!memcmp(fmt, "  services: %s public=%u", sizeof("  services: %s public=%u") - 1)) {
        va_list args;
        va_start(args, fmt);
        const char *state = va_arg(args, const char *);
        CHECK(!memcmp(state, services_stage == SERVICES_SQLITE_ONLY ? "SQLITE_ONLY" :
                      services_stage == SERVICES_READY ? "SERVICES_READY" :
                      services_stage == SERVICES_MINIMAL ? "MINIMAL" :
                      services_stage == SERVICES_MINIMAL_QUARANTINED ? "MINIMAL_QUARANTINED" : "STARTING",
                      kstrlen(state) + 1));
        CHECK(va_arg(args, unsigned) == (unsigned)services_public);
        CHECK(va_arg(args, unsigned) == (module_records[0].state == MODULE_ABSENT ?
              0 : ledger_owner_pages(OWNER)));
        CHECK(va_arg(args, unsigned) == (module_records[1].state == MODULE_ABSENT ?
              0 : ledger_owner_pages(OWNER + 1)));
        va_end(args);
        services_reports++;
    }
    if (!memcmp(fmt, "sqlite stack high-water=", 23)) {
        va_list args;
        va_start(args, fmt);
        u32 used = va_arg(args, unsigned), size = va_arg(args, unsigned);
        va_end(args);
        CHECK(!sqlite_engine_active && (host_flags & 0x200));
        CHECK(used > 0 && used < size && size == SQLITE_ENGINE_STACK_SIZE);
        stack_reports++;
        say("sqlite stack high-water="); number(used);
        say(" of "); number(size); say("\n");
    }
    if (((input[0] >= 10 && input[0] <= 36) || (input[0] >= 50 && input[0] <= 53)) && services_stage != SERVICES_READY)
        CHECK(!services_public);
    if (!memcmp(fmt, "IME: 学習不可", sizeof("IME: 学習不可") - 1)) learn_unavailable++;
    if (!memcmp(fmt, "[services] fep_mem_reserve_kb below minimum", sizeof("[services] fep_mem_reserve_kb below minimum") - 1)) {
        va_list args;
        va_start(args, fmt);
        CHECK(va_arg(args, int) == (input[0] == 34 ? 64 : 98));
        CHECK(va_arg(args, int) == OS32_SQLITE_FEP_RESERVE_MIN / 1024);
        va_end(args);
        reserve_clamped++;
    }
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
    CHECK(mode == O_RDONLY);
    if (!memcmp(path, SYS_FEP_MOD, sizeof(SYS_FEP_MOD))) {
        fep_opens++;
        return input[0] == 50 ? -1 : 4;
    }
    CHECK(!memcmp(path, SYS_SQLITE_MOD, sizeof(SYS_SQLITE_MOD)));
    opens++;
    return input[0] == 1 || (input[0] == 11 || input[0] == 57) ? -1 : 3;
}
void vfs_close(int fd) { if (fd == 4) { CHECK(!fep_closes); fep_closes++; return; } CHECK(fd == 3 && !closes); closes++; }
int vfs_fstat(int fd, OS32_Stat *st)
{
    CHECK(fd == 3 || fd == 4); st->st_size = fd == 4 ? fep_size : input[1]; return 0;
}
int vfs_read_fd(int fd, void *dst, u32 n)
{
    if (fd == 4) {
        CHECK(!fep_closes && n <= fep_size - fep_pos);
        kmemcpy(dst, fep_file + fep_pos, n); fep_pos += n; return n;
    }
    CHECK(fd == 3 && !closes && n <= input[1] - pos);
    kmemcpy(dst, file + pos, n); pos += n; return n;
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out)
{
    if (id == MODULE_ID_FEP) {
        CHECK(!fep_owned && kind == LEDGER_KIND_MODULE && name[0] == 'f');
        fep_owned = 1; *out = OWNER + 1; return 1;
    }
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
    CHECK((owner == OWNER && owned) || (owner == OWNER + 1 && fep_owned));
    CHECK(n > 0);
    alloc_calls++;
    if (input[0] == 13 || (input[0] == 52 && owner == OWNER + 1)) return 0;
    if ((input[0] == 4 && alloc_calls == 2) || (input[0] == 5 && alloc_calls == 3)) return 0;
    CHECK(allocated + n <= POOL_PAGES);
    u32 address = (u32)(uptr)(pool + allocated * PAGE_SIZE);
    for (int i = 0; i < n; i++) { live[allocated + i] = 1; page_owner[allocated + i] = owner; }
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
    CHECK((owner == OWNER || owner == OWNER + 1) && n > 0);
    u32 first = pfn - (u32)(uptr)pool / PAGE_SIZE;
    CHECK(first + n <= allocated);
    if (stack_base && first <= (stack_base - PAGE_SIZE - (u32)(uptr)pool) / PAGE_SIZE &&
        first + n > (stack_base - PAGE_SIZE - (u32)(uptr)pool) / PAGE_SIZE)
        CHECK(!guard_np && guard_restored == 1 && !paging_sqlite_stack_guard);
    if (input[0] == 3 && !prefix_failed) { prefix_failed = 1; return 0; }
    for (int i = 0; i < n; i++) { CHECK(live[first + i] && page_owner[first + i] == owner); live[first + i] = 0; }
    kmemset(pool + first * PAGE_SIZE, 0xdd, n * PAGE_SIZE);
    return 1;
}
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    if (owner == LEDGER_OWNER_KERNEL) { *pages = unicode_pages; unicode_pages = 0; return 1; }
    CHECK((owner == OWNER || owner == OWNER + 1) && !sqlite_engine_active);
    if (owner == OWNER) CHECK(module_records[1].state == MODULE_ABSENT);
    *pages = 0;
    for (u32 i = 0; i < allocated; i++) if (live[i] && page_owner[i] == owner) {
        CHECK(pgalloc_free_n_owner(owner, (u32)(uptr)pool / PAGE_SIZE + i, 1));
        ++*pages;
    }
    return 1;
}
u32 ledger_owner_pages(u32 owner)
{
    if (owner == LEDGER_OWNER_KERNEL) return unicode_pages;
    CHECK(owner == OWNER || owner == OWNER + 1);
    u32 count = 0;
    for (u32 i = 0; i < allocated; i++) count += live[i] && page_owner[i] == owner;
    return count + reused_owner_pages;
}
int ledger_owner_retire(u32 owner)
{
    CHECK(!ledger_owner_pages(owner));
    if (owner == OWNER + 1) { fep_retired++; fep_owned = 0; return 1; }
    CHECK(owner == OWNER);
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
    if (input[0] != 55 && input[0] != 19 && input[0] != 20 && (input[0] < 34 || input[0] > 36)) return;
    CHECK(!os32_sqlite_exec(db, "CREATE TABLE settings(scope TEXT,key TEXT,type INTEGER,ival INTEGER)", 0, 0, 0));
    CHECK(!os32_sqlite_exec(db, (input[0] == 19 || input[0] == 55) ?
        "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,224)" :
        input[0] == 20 ? "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,128)" :
        input[0] == 34 ? "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,64)" :
        input[0] == 35 ? "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,99)" :
                        "INSERT INTO settings VALUES('system','fep_mem_reserve_kb',0,98)", 0, 0, 0));
}
/* Inject only after the real final check sees the unpublished, ready FEP. */
int host_final_boot_check(int ready, int reserve)
{
    int rc = os32_sqlite_outer_boot_check(ready, reserve);
    if (input[0] == 54) {
        CHECK(!rc && ready == 1 && !services_public);
        CHECK(services_stage == SERVICES_FEP_INTERNAL_READY);
        CHECK(g_ime.dict_loaded && g_ime.dict.exact_stmt && fep_module_pages());
        ready_check_failed++;
        return SQLITE_ERROR;
    }
    return rc;
}
static void boot_transaction(void)
{
    services_stage = SERVICES_ROOT_READY;
    unicode_file_load();
    services_stage = SERVICES_UNICODE;
    if (input[0] > 20 && input[0] <= 31) services_boot_fault = input[0] - 20;
    if (input[0] == 58) services_boot_fault = SERVICES_FAULT_SQLITE_INIT;
    if (input[0] == 59) services_boot_fault = SERVICES_FAULT_SETTINGS_CLOSE;
    u32 unicode_before = unicode_pages;
    services_boot();
    if (input[0] == 60) CHECK(!services_public);
    CHECK(dictionary_stats == 1);
    CHECK(dictionary_missing_messages == (input[0] == 55));
    int sqlite_only = input[0] == 55;
    if (sqlite_only) {
        CHECK(opens == 1 && init_calls == 1 && !fep_opens && !fep_allocs);
        CHECK(services_stage == SERVICES_SQLITE_ONLY && sqlite_module_ready());
        CHECK(!fep_owned && !fep_module_pages() && !ime_boot_bytes());
        CHECK(module_records[1].state == MODULE_ABSENT);
        HostBudgetState *budget = (HostBudgetState *)(uptr)heap_base;
        CHECK(budget->reserve == 0);
        /* The paired engine really executes SQL, with the entire APP budget
         * available after settings closes (seeded reserve=224 KiB is ignored). */
        Os32SqliteGroup group;
        sqlite3 *db = 0;
        int selected_rows = 0;
        CHECK(!os32_sqlite_outer_group_acquire(OS32_SQLITE_EXEC, 1, &group));
        CHECK(!os32_sqlite_outer_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &db));
        CHECK(!os32_sqlite_exec(db, "SELECT 42", selected, &selected_rows, 0));
        CHECK(selected_rows == 1);
        CHECK(!os32_sqlite_exec(db, "CREATE TABLE pressure(x)", 0, 0, 0));
        int inserts = 0;
        while (os32_sqlite_exec(db, "INSERT INTO pressure VALUES(zeroblob(4096))", 0, 0, 0) == SQLITE_OK)
            CHECK(++inserts < 200);
        CHECK(budget->live[0] + budget->live[2] > 319488u);
        CHECK(!os32_sqlite_outer_group_begin_close(&group));
        CHECK(!os32_sqlite_close(db));
        CHECK(!os32_sqlite_outer_group_finish(&group, SQLITE_OK));
        ime_set_mode(1); ime_toggle();
        CHECK(!ime_is_active() && !ime_get_mode());
        CHECK(ime_switch_dict(0) == OS32_ERR_NOSYS);
        input_paths();
        say("PASS SQLITE_ONLY stat=1 sqlite=running SQL=42 FEP=stub reserve=0 app>319488\n");
    }
    services_report();
    CHECK(services_reports == 1);
    CHECK(unicode_pages == unicode_before);
    CHECK(unicode_before == ((input[0] >= 14 && input[0] <= 17) ? 0 : UNICODE_FILE_PAGES));
    CHECK(unicode_file_lookup(0x3041) == 0x2421);
    CHECK(!sqlite_engine_active && host_flags == 0x202);
    int ok = input[0] == 10 || input[0] == 19 || input[0] == 20 || (input[0] >= 32 && input[0] <= 36) || (input[0] >= 14 && input[0] <= 17) || input[0] == 56;
    int isolated = services_boot_fault == SERVICES_FAULT_SETTINGS_CLOSE ||
                   services_boot_fault == SERVICES_FAULT_FEP_CLOSE ||
                   services_boot_fault == SERVICES_FAULT_SHUTDOWN;
    CHECK(ready_check_failed == (input[0] == 54));
    CHECK(services_public == (ok || sqlite_only));
    if (ok) {
        CHECK(services_stage == SERVICES_READY && sqlite_module_ready());
        CHECK(learn_unavailable == (input[0] == 32));
        CHECK((g_ime.dict.learn_stmt == 0) == (input[0] == 32));
        CHECK(reserve_clamped == (input[0] == 34 || input[0] == 36));
        HostBudgetState *budget = (HostBudgetState *)(uptr)heap_base;
        unsigned expected_reserve = input[0] == 19 ? 224 * 1024 :
                                    input[0] == 20 ? 128 * 1024 :
                                    (input[0] >= 34 && input[0] <= 36) ? OS32_SQLITE_FEP_RESERVE_MIN :
                                    OS32_SQLITE_FEP_RESERVE;
        CHECK(budget->reserve == expected_reserve);
        CHECK(g_ime.dict_loaded && fep_allocs == 2 && !fep_frees);
        CHECK(module_records[1].state == MODULE_RUNNING && fep_module_pages());
        CHECK(module_records[1].dependency_count == 1 && module_records[1].dependencies[0].slot == 0);
        input_paths();
        void *resident_db = g_ime.dict.db;
        host_user = 1; host_key = 0x1e61;
        CHECK(ime_trygetkey() == 0x1e61);
        g_ime.kana_len = 1; g_ime.kana_buf[0] = 'a';
        ime_owner_exit(7);
        CHECK(!g_ime.kana_len && !g_ime.kana_buf[0]);
        CHECK(g_ime.dict.db == resident_db && g_ime.dict.group.generation);
        host_user = 0;
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
        if (input[0] == 20) CHECK(budget->live[0] + budget->live[2] > 319488u);
        CHECK(!os32_sqlite_bind_text(g_ime.dict.exact_stmt, 1, "nihon", -1, SQLITE_STATIC));
        CHECK(os32_sqlite_step(g_ime.dict.exact_stmt) == SQLITE_ROW);
        CHECK(os32_sqlite_column_int(g_ime.dict.exact_stmt, 1) == 42);
        CHECK(!os32_sqlite_reset(g_ime.dict.exact_stmt));
        CHECK(!os32_sqlite_outer_group_begin_close(&group));
        CHECK(!os32_sqlite_close(db));
        CHECK(!os32_sqlite_outer_group_finish(&group, SQLITE_OK));
        if (input[0] == 20) {
            CHECK(os32_sqlite_outer_boot_check(1, 128 * 1024) == SQLITE_OK);
            CHECK(budget->reserve == 128 * 1024);
        }
        say("FEP reserve="); number(budget->reserve);
        say(" app_limit="); number(319488u + OS32_SQLITE_FEP_RESERVE - budget->reserve);
        say(" diagnostics="); number(reserve_clamped); say("\n");
        say("SERVICES_READY sqlite>0 fep=736 quarantine=0 unicode retained\n");
    } else if (sqlite_only) {
        CHECK(sqlite_module_ready());
    } else if (isolated) {
        CHECK(services_stage == SERVICES_MINIMAL_QUARANTINED);
        CHECK(module_records[0].state == MODULE_QUARANTINED);
        CHECK(owned && ledger_owner_pages(OWNER) > 0 && !retired && guard_np);
        CHECK((services_boot_fault == SERVICES_FAULT_FEP_CLOSE) == (ime_boot_bytes() != 0));
        CHECK(module_records[1].state == (services_boot_fault == SERVICES_FAULT_FEP_CLOSE ?
                                         MODULE_QUARANTINED : MODULE_ABSENT));
        stubs(); input_paths();
        say("MINIMAL_QUARANTINED sqlite>0 group/heap/stack retained unicode retained\n");
    } else {
        CHECK(services_stage == SERVICES_MINIMAL);
        CHECK(module_records[0].state == MODULE_ABSENT);
        CHECK(!owned && !ledger_owner_pages(OWNER) && !ime_boot_bytes());
        CHECK(!fep_owned && !fep_module_pages() && module_records[1].state == MODULE_ABSENT);
        CHECK(fep_allocs == fep_frees && !guard_np);
        stubs(); input_paths();
        say("MINIMAL sqlite=0 fep=0 quarantine=0 unicode retained\n");
    }
    u32 pages = ledger_owner_pages(OWNER);
    u32 expected_pages = (((const u32 *)file)[MI_MEM_SIZE] + SQLITE_MODULE_POOL_SIZE +
                          SQLITE_ENGINE_STACK_SIZE + PAGE_SIZE) / PAGE_SIZE;
    CHECK(pages == ((ok || sqlite_only || isolated) ? expected_pages : 0));
    u32 fep_pages = fep_module_pages();
    CHECK(fep_pages == ((ok || services_boot_fault == SERVICES_FAULT_FEP_CLOSE) ?
                      ((const u32 *)fep_file)[MI_MEM_SIZE] / PAGE_SIZE : 0));
    CHECK(guard_np == (ok || sqlite_only || isolated));
    u32 physical_pages = 0;
    for (u32 i = 0; i < allocated; i++) physical_pages += live[i];
    CHECK(physical_pages == pages + fep_pages); /* includes the NP guard's page */
    say("occupancy sqlite_pages="); number(pages);
    say(" fep_pages="); number(fep_module_pages());
    say(" fep_bytes="); number(ime_boot_bytes());
    say(" quarantine_bytes="); number(isolated ? (pages + fep_pages) * PAGE_SIZE + ime_boot_bytes() : 0);
    say(" unicode_pages="); number(unicode_pages); say(" guard_included=1 audit_diff=0\n");
    if (services_stage == SERVICES_MINIMAL) {
        /* Reclaimed owner IDs now belong to later applications (e.g. sh).
         * Records retain those IDs, but ver must not attribute their pages. */
        reused_owner_pages = 7;
        CHECK(ledger_owner_pages(OWNER) == 7 && ledger_owner_pages(OWNER + 1) == 7);
        CHECK(!fep_module_pages());
        services_report();
        CHECK(services_reports == 2);
        reused_owner_pages = 0;
        say("PASS MINIMAL reused owner IDs: ver sqlite_pages=0 fep_pages=0\n");
    }
    u32 before = alloc_calls;
    services_boot();
    CHECK(alloc_calls == before); /* boot transaction is single-shot */
    CHECK(dictionary_stats == 1);
    if (input[0] == 33) {
        services_boot_fault = SERVICES_FAULT_FEP_CLOSE;
        CHECK(fep_module_abort() == 0);
        CHECK(!fep_owned && !fep_module_pages());
        CHECK(os32_sqlite_outer_boot_check(0, 0) == SQLITE_OK);
        say("PASS boot close fault ignored after SERVICES_READY\n");
    }
    finish(0);
}
void _start(void)
{
    read_input(input, sizeof(input)); CHECK(input[1] <= IMAGE_CAP);
    read_input(file, input[1]);
    read_input(&fep_size, sizeof(fep_size)); CHECK(fep_size <= IMAGE_CAP);
    read_input(fep_file, fep_size);
    bi.img_valid = 1; bi.img_crc = input[2];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(pool), "c"(sizeof(pool)), "d"(7) : "memory");
    CHECK(!rc);
    stubs();
    if ((input[0] >= 10 && input[0] <= 36) || (input[0] >= 50 && input[0] <= 60)) boot_transaction();
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
    if (input[0] >= 40 && input[0] <= 44) { stack_workload(db, input[0]); finish(0); }
    /* Recursive expression parsing on the real engine stack, product config. */
    CHECK(os32_sqlite_prepare_v2(db,
        "SELECT abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs("
        "abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(-42"
        "))))))))))))))))))))))))", -1, &stmt, 0) == SQLITE_OK);
    CHECK(os32_sqlite_step(stmt) == SQLITE_ROW && os32_sqlite_column_int(stmt, 0) == 42);
    CHECK(os32_sqlite_finalize(stmt) == SQLITE_OK);
    u32 deep_water = sqlite_engine_stack_high_water();
    CHECK(deep_water > 0 && deep_water < SQLITE_ENGINE_STACK_SIZE);
    char build_info[64];
    kapi_sys_get_build_info(build_info, sizeof(build_info));
    CHECK(build_info[0] && stack_reports == 1 && services_reports == 1);
    CHECK(os32_sqlite_exec(db, "DELETE FROM t", 0, 0, 0) == SQLITE_OK);
    CHECK(sqlite_engine_stack_high_water() >= deep_water);
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
    say("PASS real SQLite init/CRUD/typed diagnostics/prefix reuse/engine stack/reclaim\n");
    finish(0);
}
