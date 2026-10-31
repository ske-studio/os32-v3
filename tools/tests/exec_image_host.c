/* E01a/E01b: complete exec.c, with recorded VFS/AS and admission/CPU boundaries.
 * Image controls stop at argv; argv controls inspect the completed entry frame.
 * test_exec_r1 covers setjmp/landing. Expected bytes/order are fixed here. */
#include "types.h"
static u32 host_cr3;
#include "exec.c"

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void die(void) __attribute__((noreturn));
static void die(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(); } } while (0)

enum { OK, OPEN_FAIL, SEEK_FAIL, SHORT_FIRST, SHORT_LAST, MAP_FIRST, MAP_LAST, READ_ERROR };
static int failure, is_user, opened, closed, aborted, reads, maps, jump_ready;
static int last_slot_id;
static u32 image_size, bss_size, copied;
static char events[32];
static u32 event_count;
static u8 backing[3][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static AppSlot slots[APP_SLOT_COUNT];
static struct addrspace space, launcher_space;
static KernelAPI api;
static void *escape[5];
/* E01b lets the real loader continue through argv and the entry frame. */
static int argv_mode, read_fail_at, store_fail_at, stores, argv_tables;
static u8 argv_backing[3][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u8 command_backing[3 * PAGE_SIZE];
static u32 tramp[KAPI_DATA_IDX_SHM_BASE + 1];
static u32 expected_argv, expected_frame;
static u32 stack_phys(u32 va)
{
    u32 offset = va - (MEM_APP_STACK_TOP - sizeof(argv_backing));
    CHECK(offset < sizeof(argv_backing));
    return (u32)argv_backing[2 - offset / PAGE_SIZE] + offset % PAGE_SIZE;
}
static void argv_done(void)
{
    CHECK(argv_mode && closed == 1 && !aborted);
    CHECK(read_fail_at < 0 && store_fail_at < 0);
    CHECK(!is_user || argv_tables == 1);
    __builtin_longjmp(escape, 1);
}
static void event(char c) { CHECK(event_count + 1 < sizeof(events)); events[event_count++] = c; }
static u8 pattern(u32 offset) { return (u8)(offset * 17 + 37); }
void *kmemset(void *p, int c, u32 n) { u8 *b = p; while (n--) *b++ = c; return p; }
/* Stop at the first real app_store through its external memory boundary. */
static void reached_argv(void)
{
    CHECK(closed == 1 && failure == OK);
    CHECK(last_slot_id == (is_user ? 2 : 1));
    CHECK(slots[last_slot_id].cpl3 == is_user);
    event('V');
    __builtin_longjmp(escape, 1);
}
void *kmemcpy(void *p, const void *q, u32 n)
{
    if (!argv_mode && !is_user && (u32)p >= MEM_SHELL_STACK_TOP - MEM_SHELL_STACK_SIZE &&
        (u32)p < MEM_SHELL_STACK_TOP) reached_argv();
    u8 *d = p; const u8 *s = q;
    while (n--) *d++ = *s++;
    return p;
}
char *kstrncpy(char *d, const char *s, u32 n) { u32 i = 0; while (i + 1 < n && s[i]) { d[i] = s[i]; i++; } if (n) d[i] = 0; return d; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
char *kstrncat(char *d, const char *s, u32 n) { u32 used = kstrlen(d); kstrncpy(d + used, s, n - used); return d; }
int kstrcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }

int vfs_read(const char *path, void *buf, u32 n)
{
    CHECK((argv_mode || !kstrcmp(path, "/fixture")) && n == OS32X_HDR_SIZE);
    OS32Header *hdr = buf;
    kmemset(hdr, 0, n);
    hdr->header_size = 173; /* distinguish the payload offset from header size in memory */
    hdr->load_addr = is_user ? MEM_EXEC_LOAD_ADDR : MEM_SHELL_LOAD_ADDR;
    hdr->text_size = image_size;
    hdr->bss_size = bss_size;
    return n;
}
int vfs_open(const char *path, int mode)
{
    CHECK(jump_ready && !opened && (argv_mode || !kstrcmp(path, "/fixture")) && mode == 0);
    event('O');
    if (failure == OPEN_FAIL) return -1;
    opened = 1;
    return 7;
}
int vfs_seek(int fd, int offset, int whence)
{
    CHECK(opened && !closed && fd == 7 && offset == 173 && whence == 0);
    event('S');
    return failure == SEEK_FAIL ? -1 : offset;
}
int as_va_to_pa(u32 pd, u32 va, u32 *pa)
{
    if (va >= MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE) {
        CHECK(pd == space.pd_phys);
        if (!argv_mode) reached_argv();
        CHECK(closed == 1 && !aborted);
        if (va == expected_argv) argv_tables++;
        if (stores++ == store_fail_at) return -1;
        *pa = stack_phys(va);
        return 0;
    }
    CHECK(pd == space.pd_phys && opened && !closed);
    CHECK(va == MEM_EXEC_LOAD_ADDR + copied);
    maps++;
    event('M');
    if ((failure == MAP_FIRST && maps == 1) || (failure == MAP_LAST && maps == 2)) return -1;
    /* Reversed pages make a direct VA/contiguous-PA copy observably wrong. */
    *pa = (u32)backing[2 - ((va - MEM_EXEC_LOAD_ADDR) / PAGE_SIZE)] + (va & (PAGE_SIZE - 1));
    return 0;
}
int vfs_read_fd(int fd, void *buf, u32 n)
{
    CHECK(opened && !closed && fd == 7);
    u32 remaining = image_size - copied;
    u32 expected = remaining > PAGE_SIZE ? PAGE_SIZE : remaining;
    CHECK(n == expected && n > 0);
    CHECK(buf == (is_user ? (void *)backing[2 - copied / PAGE_SIZE] : (void *)(MEM_SHELL_LOAD_ADDR + copied)));
    reads++;
    event('R');
    if (failure == READ_ERROR) return -1;
    if ((failure == SHORT_FIRST && reads == 1) || (failure == SHORT_LAST && reads == 2)) n--;
    for (u32 i = 0; i < n; i++) ((u8 *)buf)[i] = pattern(copied + i);
    copied += n;
    return n;
}
void vfs_close(int fd) { CHECK(opened && !closed && fd == 7); closed++; event('C'); }
/* External admission/allocation and CPU boundaries; exec internals stay real. */
int paging_v86_session_open(void) { return 0; }
int v86_call_pinned(void) { return 0; }
u32 paging_current_cr3(void) { return argv_mode && is_user ? 17 : 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd)
{
    if (argv_mode && is_user && pd == 17) {
        CHECK(aborted == 1 && last_slot_id == 3 && !slots[2].as);
        return; /* real abort has torn down the child before restoring parent */
    }
    if (argv_mode && pd == space.pd_phys) argv_done();
    CHECK(pd == 1);
    /* The real exec_launch_abort switches to master before teardown. */
    if (jump_ready && !aborted) {
        CHECK(last_slot_id == (is_user ? 2 : 1));
        CHECK((failure != OK || (argv_mode && (read_fail_at >= 0 || store_fail_at >= 0))) && (!opened || closed == 1));
        aborted++; event('A');
    } else if (jump_ready) CHECK(last_slot_id == 1); /* restore the caller */
}
void shell_print(const char *s, u8 a) { (void)s; (void)a; CHECK(0); }
void shell_print_dec(u32 n, u8 a) { (void)n; (void)a; CHECK(0); }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; CHECK(0); }
int appslot_cur(void) { return argv_mode && is_user ? 3 : 1; }
AppSlot *appslot_at(int id) { CHECK(id >= 0 && id < APP_SLOT_COUNT); last_slot_id = id; return &slots[id]; }
int appslot_start_admit(int gui, u32 pages, u32 free_pages) { (void)gui; (void)pages; (void)free_pages; return 2; }
int appslot_cui_only_admit(int gui, u32 flags) { (void)gui; (void)flags; return 0; }
int os32x_layout_check(const OS32Header *h, u32 n, u32 off) { (void)h; (void)n; (void)off; return OS32X_LAYOUT_OK; }
const char *os32x_layout_reason(int rc) { (void)rc; CHECK(0); return 0; }
u32 pgalloc_free_pages(void) { return 100; }
void exec_heap_save_state(u32 *used) { *used = 0; }
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out) { (void)kind; (void)id; (void)name; *out = 9; return 1; }
int ledger_owner_retire(u32 owner) { CHECK(owner == 9); return 1; }
void *kmalloc(u32 n) { CHECK(n == sizeof(space)); return &space; }
void kfree(void *p) { CHECK(p == &space); }
int paging_addrspace_create_lease(struct addrspace *as, u32 owner) { CHECK(as == &space && owner == 9); as->pd_phys = 23; as->owner = owner; return 0; }
void appmem_init(struct addrspace *as, u32 b, u32 e, u32 h, u32 g) { (void)as; (void)b; (void)e; (void)h; (void)g; }
int paging_addrspace_clear_app_band(struct addrspace *as) { CHECK(as == &space); return 0; }
int shlib_addrspace_attach(struct addrspace *as) { CHECK(as == &space); return 0; }
int exec_setjmp(u32 *buf) { (void)buf; CHECK(!jump_ready); jump_ready = 1; event('J'); return 0; }
void exec_heap_user_init(struct addrspace *as) { (void)as; CHECK(0); }
void exec_heap_init_at(u32 base, u32 size) { (void)base; (void)size; if (argv_mode) argv_done(); CHECK(0); }
void appslot_shell_commit(void) { CHECK(0); }
void shell_service_retire(void) { CHECK(0); }
void shell_service_publish(u32 base, u32 size) { (void)base; (void)size; CHECK(0); }
void appslot_start_commit(int id, int gui, u32 pages) { (void)id; (void)gui; (void)pages; CHECK(0); }
void appslot_mark_scheduled(int id, u32 tick) { (void)id; (void)tick; CHECK(0); }

ShellService g_shell_service;
int vfs_resolve_path(const char *path, char *out, int size) { kstrncpy(out, path, size); return VFS_OK; }
volatile u32 font_guard_active, tick_count, ring3_wm_shell_gen;
u32 shlib_data_pages(void) { return 0; }
int shlib_loaded(void) { return 0; }
int as_va_to_pa_read(u32 pd, u32 va, u32 *pa)
{
    CHECK(argv_mode && pd == 17 && va >= MEM_EXEC_LOAD_ADDR);
    u32 offset = va - MEM_EXEC_LOAD_ADDR;
    CHECK(offset < sizeof(command_backing));
    /* The first scan succeeds; fail only on the post-image argv read. */
    if (closed && (int)offset == read_fail_at) return -1;
    *pa = (u32)command_backing + offset;
    return 0;
}
/* Mapping is outside this test: image reads use the reversed backing above. */
static u8 allocation[MEM_EXEC_STACK_SIZE] __attribute__((aligned(PAGE_SIZE)));
u32 pgalloc_alloc_phys(u32 owner, int n) { CHECK(owner == 9 && n > 0 && (u32)n <= sizeof(allocation) / PAGE_SIZE); return (u32)allocation; }
int paging_addrspace_map_user_range_phys(struct addrspace *as, u32 b, u32 e, u32 pa, u32 flags) { (void)b; (void)e; (void)flags; CHECK(as == &space && pa == (u32)allocation); return 0; }
int paging_addrspace_map_user(struct addrspace *as, u32 va, u32 pa, u32 flags) { (void)as; (void)va; (void)pa; (void)flags; CHECK(0); return -1; }
int pgalloc_free_n_owner(u32 owner, u32 start, int n) { (void)owner; (void)start; (void)n; CHECK(0); return 0; }
void caller_access_save(volatile CallerAccessFrame *out) { kmemset((void *)out, 0, sizeof(*out)); }
u32 tss_get_esp0(void) { return 0; }
u32 ledger_owner_pages(u32 owner) { CHECK(owner == 9); return 0; }
int lease_revoke_all(struct addrspace *as) { CHECK(as == &space); return 0; }
void shlib_addrspace_detach(struct addrspace *as) { CHECK(as == &space); }
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 b, u32 e) { CHECK(as == &space); return (e - b) / PAGE_SIZE; }
void paging_addrspace_destroy(struct addrspace *as) { CHECK(as == &space); as->pd_phys = 0; }
void paging_addrspace_poison(struct addrspace *as) { (void)as; CHECK(0); }
int ledger_reclaim_owner(u32 owner, u32 *pages) { CHECK(owner == 9); *pages = 0; return 1; }
void serial_puts_polled(const char *s) { (void)s; }
volatile u32 kctx_irq_depth, kctx_exc_depth;
void exec_heap_restore_state(u32 base, u32 size, u32 used) { (void)base; (void)size; (void)used; }
int paging_set_not_present(u32 b, u32 e) { (void)b; (void)e; return 0; }

static void check_case(int user, int fault, u32 text)
{
    is_user = user; failure = fault; image_size = text; bss_size = 33;
    opened = closed = aborted = reads = maps = jump_ready = 0;
    copied = event_count = 0;
    kmemset(events, 0, sizeof(events));
    kmemset(slots, 0, sizeof(slots));
    kmemset(backing, 0xA5, sizeof(backing));
    kmemset((void *)MEM_SHELL_LOAD_ADDR, 0xA5, 3 * PAGE_SIZE);
    kapi = &api;
    exec_nest_level = user;
    int escaped = __builtin_setjmp(escape);
    if (!escaped) {
        int rc;
        if (!text) {
            /* Full admission rejects an empty executable. Keep the original
             * zero-payload/BSS I/O control at the loader helper's boundary. */
            AppSlot *ctx = &slots[user ? 2 : 1];
            OS32Header hdr = {.header_size = 173, .text_size = 0, .bss_size = bss_size};
            ctx->cpl3 = user; ctx->as = &space;
            ctx->load_addr = user ? MEM_EXEC_LOAD_ADDR : MEM_SHELL_LOAD_ADDR;
            space.pd_phys = 23;
            exec_setjmp(ctx->jmpbuf);
            CHECK(exec_load_image(ctx, "/fixture", &hdr) == 0);
            CHECK(closed == 1); event('V');
            __builtin_longjmp(escape, 1);
        }
        rc = exec_launch("/fixture", 0);
        CHECK(fault != OK && aborted == 1);
        CHECK(rc == (fault == OPEN_FAIL ? EXEC_ERR_NOT_FOUND : EXEC_ERR_INVALID));
    } else CHECK(fault == OK && !aborted && copied == text);
    const char *expected;
    switch (fault) {
    case OPEN_FAIL: expected = "JOA"; break;
    case SEEK_FAIL: expected = "JOSCA"; break;
    case SHORT_FIRST: case READ_ERROR: expected = user ? "JOSMRCA" : "JOSRCA"; break;
    case SHORT_LAST: expected = user ? "JOSMRMRCA" : "JOSRRCA"; break;
    case MAP_FIRST: expected = "JOSMCA"; break;
    case MAP_LAST: expected = "JOSMRMCA"; break;
    default: expected = text == 0 ? "JOSCV" : text <= PAGE_SIZE ?
        (user ? "JOSMRCV" : "JOSRCV") : (user ? "JOSMRMRCV" : "JOSRRCV"); break;
    }
    CHECK(!kstrcmp(events, expected));
    for (u32 off = 0; off < 3 * PAGE_SIZE; off++) {
        u8 actual = user ? backing[2 - off / PAGE_SIZE][off % PAGE_SIZE] : ((u8 *)MEM_SHELL_LOAD_ADDR)[off];
        u8 expected_byte = off < copied ? pattern(off) :
            (!user && fault == OK && off < text + bss_size ? 0 : 0xA5);
        CHECK(actual == expected_byte);
    }
}
static u8 stack_byte(int user, u32 va)
{
    return *(const u8 *)(user ? stack_phys(va) : va);
}
static u32 stack_word(int user, u32 va)
{
    u32 value = 0;
    for (u32 i = 0; i < 4; i++) value |= (u32)stack_byte(user, va + i) << (8 * i);
    return value;
}
static void argv_case(int user, const char *command, const char *bytes,
                      u32 byte_count, int argc, int read_fault, int store_fault)
{
    argv_mode = 1; is_user = user; failure = OK; image_size = 1; bss_size = 33;
    opened = closed = aborted = reads = maps = jump_ready = 0;
    copied = event_count = 0; stores = argv_tables = 0;
    read_fail_at = read_fault; store_fail_at = store_fault;
    kmemset(slots, 0, sizeof(slots));
    kmemset(argv_backing, 0xA5, sizeof(argv_backing));
    kmemset((void *)(MEM_SHELL_STACK_TOP - sizeof(argv_backing)), 0xA5, sizeof(argv_backing));
    launcher_space.pd_phys = 17;
    slots[3].as = &launcher_space; slots[3].cpl3 = 1;
    slots[3].band_pdes = 1; slots[3].depth = 1;
    kapi = &api; exec_nest_level = user; ring3_tramp_page = (u32)tramp;
    api.shm_base = 0x12345000;
    u32 top = user ? MEM_APP_STACK_TOP : MEM_SHELL_STACK_TOP;
    u32 str = (top - kstrlen(command) - 1) & ~3u;
    expected_argv = str - 4 * OS32_MAX_ARGS;
    expected_frame = (expected_argv - 16) & ~15u;
    const char *input = command;
    if (user) {
        /* Raw path parsing sees the same bytes, while launch_read_byte must
         * use the launcher PD and the separate physical backing. */
        kstrncpy((char *)MEM_EXEC_LOAD_ADDR, command, sizeof(command_backing));
        kstrncpy((char *)command_backing, command, sizeof(command_backing));
        input = (const char *)MEM_EXEC_LOAD_ADDR;
    }
    if (!__builtin_setjmp(escape)) {
        int rc = exec_launch(input, 0);
        CHECK(read_fault >= 0 || store_fault >= 0);
        CHECK(rc == EXEC_ERR_INVALID && aborted == 1);
        CHECK(slots[user ? 2 : 1].as == 0);
        if (user) CHECK(g_cur_app == &slots[3]);
        if (read_fault >= 0 || (store_fault >= 0 && store_fault < (int)byte_count))
            CHECK(argv_tables == 0); /* failed string copy short-circuits table */
        return;
    }
    CHECK(read_fault < 0 && store_fault < 0);
    CHECK(slots[user ? 2 : 1].stack_top == top);
    u32 offset = 0;
    for (int i = 0; i < argc; i++) {
        CHECK(stack_word(user, expected_argv + 4 * i) == str + offset);
        while (bytes[offset++]) {}
    }
    CHECK(stack_word(user, expected_argv + 4 * argc) == 0);
    for (u32 i = 0; i < byte_count; i++) CHECK(stack_byte(user, str + i) == (u8)bytes[i]);
    /* No writes outside the strings, the used argv table and the frame. */
    for (u32 va = top - sizeof(argv_backing); va < top; va++) {
        if ((va >= str && va < str + byte_count) ||
            (va >= expected_argv && va < expected_argv + 4 * (argc + 1)) ||
            (va >= expected_frame - (user ? 4 : 0) && va < expected_frame + 16)) continue;
        CHECK(stack_byte(user, va) == 0xA5);
    }
    CHECK(stack_word(user, expected_frame) == (u32)argc);
    CHECK(stack_word(user, expected_frame + 4) == expected_argv);
    CHECK(stack_word(user, expected_frame + 8) == (user ? (u32)tramp : (u32)kapi));
    CHECK(stack_word(user, expected_frame + 12) == (user ? 0 : (u32)&g_shell_service));
    if (user) CHECK(stack_word(user, expected_frame - 4) == 0);
}
static void argv_cases(void)
{
    for (int user = 0; user <= 1; user++) {
        argv_case(user, "", "", 0, 0, -1, -1);
        argv_case(user, "   ", "", 0, 0, -1, -1);
        argv_case(user, "/fixture one  two ", "/fixture\0one\0two", 17, 3, -1, -1);
        argv_case(user, "/fixture \"a b\" 'c d' e\\ f \"g\\\"h\" 'i\\j'", "/fixture\0a b\0c d\0e f\0g\"h\0i\\j", 29, 6, -1, -1);
        argv_case(user, "/fixture a\"b c\"d '' \"\" end\\", "/fixture\0ab cd\0\0\0end\\", 22, 5, -1, -1);
        argv_case(user, "/fixture \"open end", "/fixture\0open end", 18, 2, -1, -1);
        argv_case(user, "/fixture 'open end", "/fixture\0open end", 18, 2, -1, -1);
        /* More tokens than argv can hold still copy every byte and NUL. */
        char command[2 * (OS32_MAX_ARGS + 2) + 1], bytes[2 * (OS32_MAX_ARGS + 2)];
        u32 n = 0;
        for (int i = 0; i < OS32_MAX_ARGS + 2; i++) {
            command[n] = bytes[n] = 'x'; n++;
            command[n] = ' '; bytes[n++] = 0;
        }
        command[n] = 0;
        argv_case(user, command, bytes, n, OS32_MAX_ARGS - 1, -1, -1);
        /* One token crosses a page; the terminator and argv use other pages. */
        static char long_command[PAGE_SIZE + 19];
        for (u32 i = 0; i < sizeof(long_command) - 1; i++) long_command[i] = 'z';
        long_command[8] = ' '; long_command[sizeof(long_command) - 1] = 0;
        static char long_bytes[sizeof(long_command)];
        kmemcpy(long_bytes, long_command, sizeof(long_bytes)); long_bytes[8] = 0;
        argv_case(user, long_command, long_bytes, sizeof(long_bytes), 2, -1, -1);
    }
    /* Each character/NUL store, table store, then frame store must abort. */
    for (int at = 0; at < 14; at++)
        argv_case(1, "/fixture ab", "/fixture\0ab", 12, 2, -1, at);
    for (int at = 0; at <= 11; at++)
        argv_case(1, "/fixture ab", "/fixture\0ab", 12, 2, at, -1);
    argv_case(1, "/fixture \"a\\b\"", "/fixture\0ab", 12, 2, 12, -1);
    say("PASS exec argv: 45 launches, quoting/escape/limits/pages, read/store errors, frame and abort\n");
}
void _start(void)
{
    u32 result, args[6] = {MEM_SHELL_LOAD_ADDR, 3 * PAGE_SIZE, 3, 0x32, 0xffffffffu, 0};
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_SHELL_LOAD_ADDR);
    args[0] = MEM_SHELL_STACK_TOP - sizeof(argv_backing); args[1] = sizeof(argv_backing);
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == args[0]);
    args[0] = MEM_EXEC_LOAD_ADDR;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == args[0]);
    for (int user = 0; user <= 1; user++) {
        for (int fault = OK; fault <= READ_ERROR; fault++) {
            if (!user && (fault == MAP_FIRST || fault == MAP_LAST)) continue;
            check_case(user, fault, PAGE_SIZE + 17);
        }
        check_case(user, OK, PAGE_SIZE);
        check_case(user, OK, 1);
        check_case(user, OK, 0);
    }
    say("PASS exec image: 20 cases (18 launches, 2 empty-payload controls), bytes, page boundaries, BSS and close-before-abort\n");
    argv_cases();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
