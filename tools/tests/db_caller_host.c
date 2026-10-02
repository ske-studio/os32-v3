/* Actual dispatcher, DB/output wrappers, VFS and caller copy/walk.
 * Only SQLite and the filesystem driver are entry-recording boundaries. */
#define HOST_DB_CALLER_TEST
#define HOST_CALLER_COPY_TEST
#include "access_walk_host.c"
#include "ring3_str.h"
volatile int ring3_in_syscall = 1;
static AppSlot *g_cur_app;
#include "guards_host_source.c"
static void *kill_env[5];
static int expect_kill, invoke_calls;
void ring3_fault_kill(void) {
    CHECK(expect_kill);
    __builtin_longjmp(kill_env, 1);
}
#define KAPI_HIT(n) ((void)(n))
#include "out_wrapper_host_source.c"
#include "db_host_source.c"
#include "vfs_host_source.c"
#include "guest_probe_host_source.c"
#include "arg_tables_host_source.c"
static u32 *g_cur_frame;
static u32 tick_count;
volatile u32 ring3_caller_reject_count;
static void ring3_abort_check(void) {}
static void ring3_gui_pump(void) {}
static int stop_park_calls;
static void exec_park_stop(u32 *frame) { (void)frame; stop_park_calls++; }
static u32 kapi_invoke(void *fn, const void *args, u32 n) {
    const u32 *a = args;
    (void)n;
    invoke_calls++;
    return ((u32 (*)(u32, u32, u32, u32, u32, u32))fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
}
static u32 host_kapi_table[KAPI_FUNC_COUNT + 2];
#define KAPI_ADDR host_kapi_table
#define RING3_ARG_WINDOW 64u
#include "dispatch_host_source.c"

/* Exercise the real early gate and wrappers with a mapped USER argument window. */
static int dispatch_probe(u32 slot_id, u32 a, u32 b, u32 c, u32 d, int kill) {
    u32 frame[13] = {0};
    u32 *args = (u32 *)MEM_EXEC_LOAD_ADDR;
    args[1] = a; args[2] = b; args[3] = c; args[4] = d;
    frame[7] = slot_id; frame[11] = (u32)args;
    int parks_before = stop_park_calls;
    expect_kill = kill;
    if (__builtin_setjmp(kill_env)) {
        CHECK(stop_park_calls == parks_before);
        expect_kill = 0;
        return -999;
    }
    ring3_syscall_dispatch(frame);
    CHECK(!kill && stop_park_calls == parks_before + 1);
    return (int)frame[7];
}

static u32 sqlite_calls, vfs_calls;
static const char *expected_input;
static u32 expected_if;
static void sql_entry(void) {
    CHECK(host_arch_if == expected_if);
    sqlite_calls++;
}
static void copied(const char *s) {
    CHECK(s != expected_input);
    if (kstrcmp(s, expected_input)) { report(s, kstrlen(s)); SAY(" != expected"); }
    CHECK(!kstrcmp(s, expected_input));
    CHECK((uptr)s < MEM_SHM_BASE || (uptr)s >= MEM_SHM_BASE + MEM_SHM_SIZE);
}
int sqlite3_open(const char *s, sqlite3 **out) {
    sql_entry(); copied(s); *out = (sqlite3 *)1; return SQLITE_OK;
}
int sqlite3_open_v2(const char *s, sqlite3 **out, int f, const char *v) {
    (void)f; (void)v; sql_entry();
    CHECK(s == abs_path_buf && s[0] == '/');
    copied(expected_input[0] == '/' ? s : s + 1);
    *out = (sqlite3 *)1; return SQLITE_OK;
}
int sqlite3_prepare_v2(sqlite3 *db, const char *s, int n, sqlite3_stmt **out, const char **tail) {
    (void)db; (void)n; sql_entry(); copied(s); *out = (sqlite3_stmt *)2;
    if (tail) *tail = s + kstrlen(s);
    return SQLITE_OK;
}
int sqlite3_exec(sqlite3 *db, const char *s, int (*cb)(void *, int, char **, char **), void *a, char **e) {
    (void)db; (void)s; (void)cb; (void)a; (void)e; sql_entry(); return SQLITE_OK;
}
int sqlite3_finalize(sqlite3_stmt *s) { (void)s; sql_entry(); return SQLITE_OK; }
int sqlite3_close(sqlite3 *db) { (void)db; sql_entry(); return SQLITE_OK; }
int sqlite3_get_autocommit(sqlite3 *db) { (void)db; sql_entry(); return 1; }
int sqlite3_extended_errcode(sqlite3 *db) { (void)db; sql_entry(); return SQLITE_OK; }
const char *sqlite3_errmsg(sqlite3 *db) { (void)db; sql_entry(); return "error"; }
const char *sqlite3_errstr(int rc) { (void)rc; sql_entry(); return "error"; }
int sqlite3_step(sqlite3_stmt *s) { (void)s; sql_entry(); return SQLITE_DONE; }
const unsigned char *sqlite3_column_text(sqlite3_stmt *s, int c) {
    (void)s; (void)c; sql_entry(); return (const unsigned char *)"delete";
}
int sqlite3_bind_parameter_count(sqlite3_stmt *s) { (void)s; sql_entry(); return 1; }
int sqlite3_bind_text(sqlite3_stmt *s, int i, const char *t, int n, void (*d)(void *)) {
    (void)s; (void)i; (void)d; sql_entry();
    CHECK(n == 4 && t[0] == 'a' && t[3] == 'd'); return SQLITE_OK;
}
int sqlite3_bind_blob(sqlite3_stmt *s, int i, const void *t, int n, void (*d)(void *)) {
    return sqlite3_bind_text(s, i, t, n, d);
}
static int host_stat(void *ctx, const char *s, OS32_Stat *st) {
    (void)ctx;
    vfs_calls++; CHECK(host_arch_if == expected_if);
    if (kstrlen(s) > 8) return OS32_ERR_NOTFOUND;
    st->st_size = PAGE_SIZE; return 0;
}
static VfsOps host_ops = {.stat = host_stat};
/* Real ring3_str.c is independently tested; guard predicate is its real body. */
#include "str_host_source.c"

static void reset_db(void) {
    kmemset(db_slots, 0, sizeof(db_slots));
    db_slots[0].in_use = 1; db_slots[0].owner = current;
    db_slots[0].db = (sqlite3 *)1; db_slots[0].active_stmt = (sqlite3_stmt *)3;
    db_slots[0].bindable = 1;
    sqlite_calls = vfs_calls = 0;
}
static void caller_copy_tests(void)
{
    CallerAccessFrame prev;
    u32 va = MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 4;
    u8 *p = P2V(payload);
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT;
    u32 args[6] = {MEM_EXEC_LOAD_ADDR, PAGE_SIZE, 3, 0x32, 0xffffffff, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_EXEC_LOAD_ADDR);
    mounts[0].in_use = 1;
    kstrncpy(mounts[0].prefix, "/", VFS_MAX_PATH);
    mounts[0].ops = &host_ops;
    ((u32 *)KAPI_ADDR)[2 + KAPI_SLOT_DB_BIND_TEXT] = (u32)kapi_db_bind_text;
    ((u32 *)KAPI_ADDR)[2 + KAPI_SLOT_SYS_STAT] = (u32)wrap_sys_stat;
    g_cur_app = &slot;
    slot.stack_base = MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE;
    slot.stack_top = MEM_APP_STACK_TOP;
    kmemcpy((void *)va, "bad", 4); /* VA is accessible but different from PA. */
    expected_input = "abc";
    CHECK(caller_access_enter(&prev, CALLER_USER));
    for (u32 f = 0; f < 2; f++) {
        expected_if = host_arch_if = f ? 0x202 : 2;
        u32 root = host_cr3;
        /* The guest probe must reach DB range rejection, never the early kill.
         * The historical 0x7fffff reproduces a kill WITHOUT a range-counter bump. */
        KernelAPI api = {0};
        api.sbrk_heap_limit = MEM_EXEC_LOAD_ADDR + PAGE_SIZE;
        reset_db();
        u32 before = ring3_range_reject_count;
        int invoked = invoke_calls;
        CHECK(dispatch_probe(KAPI_SLOT_DB_BIND_TEXT, 0, 1, 0x7fffff, 2, 1) == -999);
        CHECK(invoke_calls == invoked && ring3_range_reject_count == before);
        CHECK(dispatch_probe(KAPI_SLOT_DB_BIND_TEXT, 0, 1,
              (u32)guard_crossing_text(&api), 2, 0) == -1);
        CHECK(invoke_calls == invoked + 1 && ring3_range_reject_count == before);
        /* Public sys_stat still checks the app output before driver writes. */
        kmemcpy((void *)(MEM_EXEC_LOAD_ADDR + 128), "/abc", 5);
        u32 out = MEM_EXEC_LOAD_ADDR + 256;
        CHECK(dispatch_probe(KAPI_SLOT_SYS_STAT, MEM_EXEC_LOAD_ADDR + 128,
              out, 0, 0, 0) == 0);
        CHECK(((OS32_Stat *)out)->st_size == PAGE_SIZE);
        pt[index] &= ~PTE_RW;
        u32 stat_before = vfs_calls;
        ((OS32_Stat *)out)->st_size = 123;
        CHECK(dispatch_probe(KAPI_SLOT_SYS_STAT, MEM_EXEC_LOAD_ADDR + 128,
              out, 0, 0, 1) == -999);
        CHECK(vfs_calls == stat_before && ((OS32_Stat *)out)->st_size == 123);
        CHECK(ring3_range_reject_addr == out);
        pt[index] |= PTE_RW;
        for (int entry = 0; entry < 3; entry++) {
            reset_db(); kmemcpy(p + PAGE_SIZE - 4, "abcd", 4);
            CHECK(ring3_ptr_ok(va));
            int rc = entry == 0 ? kapi_db_open((void *)va) : entry == 1 ?
                kapi_db_open_existing((void *)va, 0) : kapi_db_prepare_only(0, (void *)va);
            CHECK(rc == -1 && sqlite_calls == (entry == 2 ? 1u : 0u) && !vfs_calls);
            if (entry == 2) {
                CHECK(!db_slots[0].active_stmt && !db_slots[0].bindable);
            } else {
                CHECK(db_slots[0].active_stmt == (sqlite3_stmt *)3 && db_slots[0].bindable);
            }
            CHECK(host_arch_if == expected_if && host_cr3 == root);
            p[PAGE_SIZE - 1] = 0; expected_input = "abc";
            u32 rejects = ring3_range_reject_count;
            rc = entry == 0 ? kapi_db_open((void *)va) : entry == 1 ?
                kapi_db_open_existing((void *)va, 0) : kapi_db_prepare_only(0, (void *)va);
            CHECK(rc >= 0 && sqlite_calls);
            CHECK(ring3_range_reject_count == rejects);
            if (entry == 1) CHECK(vfs_calls == 2); /* real VFS -> kernel local st */
            CHECK(host_arch_if == expected_if && host_cr3 == root);
        }
        reset_db(); kmemcpy(p + PAGE_SIZE - 4, "abcd", 4);
        CHECK(kapi_db_bind_text(0, 1, (void *)va, 5) == -1);
        CHECK(kapi_db_bind_blob(0, 1, (void *)va, 5) == -1);
        CHECK(kapi_db_bind_blob(0, 1, 0, 0) == -1);
        /* Parameter count is checked before copying, as in the old wrapper. */
        sqlite_calls = 0;
        CHECK(!kapi_db_bind_text(0, 1, (void *)va, 4));
        CHECK(!kapi_db_bind_blob(0, 1, (void *)va, 4));
        CHECK(ring3_user_range_ok(va, 4));
        CHECK(!ring3_user_range_ok(va, 5));
        CHECK(ring3_user_ranges_writable(va, 4, va, 4));
        CHECK(!ring3_user_ranges_writable(va, 4, va + 1, 4));
        CHECK(ring3_range_reject_addr == va + 1);
        CHECK(!ring3_user_range_ok(~(u32)0 - 1, 4));
        pt[index] &= ~PTE_RW;
        CHECK(ring3_user_range_ok(va, 4));
        CHECK(!ring3_user_range_writable(va, 4));
        pt[index] |= PTE_RW;
        ((u32 *)P2V(space.pd_phys))[APP_BAND_PDE] &= ~PTE_RW;
        CHECK(!ring3_user_range_writable(va, 4));
        ((u32 *)P2V(space.pd_phys))[APP_BAND_PDE] |= PTE_RW;
        ring3_wm_depth = 1;
        CHECK(ring3_user_ranges_writable(1, 4, 1, 4));
        CHECK(ring3_user_range_ok(1, 4));
        CHECK(!ring3_user_ranges_writable_always(va, 4, va, 5));
        CHECK(ring3_user_ranges_writable_always(va, 4, va, 4));
        ring3_wm_depth = 0;
        /* Return scratch from getcwd: RO USER trampoline, actual string copy. */
        u32 tramp = exec_tramp_page_addr(), scratch = tramp + RING3_USTR_OFF;
        u32 *shared = P2V(paging_registered_pt(tramp));
        shared[(tramp >> PAGE_SHIFT) % PTE_COUNT] = tramp | PTE_PRESENT | PTE_USER;
        ((u32 *)P2V(space.pd_phys))[tramp >> 22] |= PTE_USER;
        kmemcpy(P2V(scratch), "abc", 4); expected_input = "abc";
        CHECK(ring3_ptr_ok(scratch) && ring3_user_range_ok(scratch, 4));
        CHECK(!ring3_user_range_writable(scratch, 4));
        reset_db(); CHECK(kapi_db_open((void *)scratch) >= 0 && sqlite_calls);
        /* Explicit direct CPL0 and WM calls use bounded trusted copies. */
        kmemcpy(p, "abc", 4);
        ring3_in_syscall = 0; reset_db();
        CHECK(kapi_db_open((void *)p) >= 0 && sqlite_calls);
        ring3_in_syscall = 1; ring3_wm_depth = 1; reset_db();
        CHECK(kapi_db_open((void *)p) >= 0 && sqlite_calls);
        ring3_wm_depth = 0;
        caller_access_invalidate(); reset_db();
        CHECK(kapi_db_open((void *)va) == -1 && !sqlite_calls);
        /* Boot kselftest analogue: dispatch flag set, saved caller invalid. */
        u32 base = ring3_range_reject_count;
        CHECK(!ring3_user_range_writable(va, 1));
        CHECK(ring3_range_reject_count == base + 1 &&
              ring3_range_reject_last == RING3_RANGE_WR_TABLE &&
              ring3_range_reject_addr == va && ring3_range_reject_page == 0);
        CHECK(!ring3_user_range_ok(va, 1));
        CHECK(ring3_range_reject_count == base + 2 &&
              ring3_range_reject_last == RING3_RANGE_BAND);
        for (int mode = 0; mode < 2; mode++) {
            reset_db();
            CHECK(kapi_db_prepare_only(0, mode ? (void *)va : 0) == -1);
            CHECK(sqlite_calls == 1 && !db_slots[0].active_stmt && !db_slots[0].bindable);
        }
        CHECK(caller_access_enter(&prev, CALLER_USER));
        CHECK(host_arch_if == expected_if && host_cr3 == root);
    }
    struct ledger_surface sf = {.first = payload / PAGE_SIZE, .npages = 1,
        .owner = space.owner, .backing = LEDGER_SB_RAM, .width = 1, .height = 1,
        .pitch = 1, .planes = 1, .backend = LEDGER_SF_PC98,
        .role = LEDGER_ROLE_CLIENT, .perm_max = LEDGER_PERM_RO};
    u32 sid;
    host_cr3 = paging_kernel_pd_phys();
    CHECK(ledger_surface_create(&sf, &sid));
    host_cr3 = space.pd_phys;
    struct ledger_surface *live = &ledger_surfaces[sid];
    live->lease_count = 1;
    space.leases[0] = (struct as_lease){1, sid, live->gen, MEM_LEASE_BASE, 1, PAGE_RO | PTE_USER};
    ((u32 *)P2V(space.lease_pt_phys[0]))[0] = payload | PAGE_RO | PTE_USER;
    CHECK(ring3_ptr_ok(MEM_LEASE_BASE));
    CHECK(ring3_user_range_ok(MEM_LEASE_BASE, 4));
    CHECK(!ring3_user_range_writable(MEM_LEASE_BASE, 4));
    kmemcpy(p, "abc", 4); reset_db(); expected_input = "abc";
    CHECK(kapi_db_open((void *)MEM_LEASE_BASE) >= 0 && sqlite_calls);
    live->gen++;
    CHECK(!ring3_ptr_ok(MEM_LEASE_BASE));
    CHECK(!ring3_user_range_ok(MEM_LEASE_BASE, 4));
    SAY("PASS: d5 real DB/copy/walk, guest dispatch boundary, internal VFS/public stat, RO/scratch/output/WM/IF/CR3");
    die(0);
}

void *kmemset(void *d, int v, u32 n) { u8 *p = d; while (n--) *p++ = v; return d; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
int kstrcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
int kstrncmp(const char *a, const char *b, u32 n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (u8)*a - (u8)*b : 0; }
char *kstrncpy(char *d, const char *s, u32 n) { u32 i = 0; if (n) { for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; } return d; }
char *kstrncat(char *d, const char *s, u32 n) { u32 count = kstrlen(d); if (count < n) kstrncpy(d + count, s, n - count); return d; }
