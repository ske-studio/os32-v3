/* E01a: full exec_launch, with recorded VFS/AS and admission/CPU boundaries.
 * Success stops at the first argv store; setjmp/landing itself is covered by
 * test_exec_r1. Expected bytes and call order are independent of the loader. */
#include "types.h"
#include "exec.h"
#include "appslot.h"
#include "config.h"
#include "os32x_hdr.h"
#include "vfs.h"
#include "kstring.h"
#include "pgalloc.h"
#include "cpu.h"

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
static u32 image_size, bss_size, copied;
static char events[32];
static u32 event_count;
static u8 backing[3][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static AppSlot slots[APP_SLOT_COUNT];
static struct addrspace space;
static KernelAPI api, *kapi = &api;
static AppSlot *g_cur_app;
static u32 g_ring3_band_top, g_ring3_band_pdes, ring3_tramp_page, tick_count;
static int font_guard_active, g_layout_reject, g_longjmp_reason, g_longjmp_id;
volatile int exec_nest_level;
static int exec_exit_status, exec_entry_calls, g_shell_service;
typedef int Ring3CallContext;
#define EXEC_LJ_PARK 2
static void *escape[5];
static void event(char c) { CHECK(event_count + 1 < sizeof(events)); events[event_count++] = c; }
static u8 pattern(u32 offset) { return (u8)(offset * 17 + 37); }
void *kmemset(void *p, int c, u32 n) { u8 *b = p; while (n--) *b++ = c; return p; }
void *kmemcpy(void *p, const void *q, u32 n) { u8 *d = p; const u8 *s = q; while (n--) *d++ = *s++; return p; }
char *kstrncpy(char *d, const char *s, u32 n) { u32 i = 0; while (i + 1 < n && s[i]) { d[i] = s[i]; i++; } if (n) d[i] = 0; return d; }
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
char *kstrncat(char *d, const char *s, u32 n) { u32 used = kstrlen(d); kstrncpy(d + used, s, n - used); return d; }
int kstrcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }

int vfs_read(const char *path, void *buf, u32 n)
{
    CHECK(!kstrcmp(path, "/fixture") && n == OS32X_HDR_SIZE);
    OS32Header *hdr = buf;
    kmemset(hdr, 0, n);
    hdr->header_size = 173; /* distinguish the payload offset from header size in memory */
    hdr->text_size = image_size;
    hdr->bss_size = bss_size;
    return n;
}
int vfs_open(const char *path, int mode)
{
    CHECK(jump_ready && !opened && !kstrcmp(path, "/fixture") && mode == 0);
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
static int exec_launch_abort(int parent, int id, int rc)
{
    CHECK(parent == 1 && id == (is_user ? 2 : 1));
    CHECK(!opened || closed == 1); /* close must precede caller's abort */
    CHECK(!aborted++); event('A'); return rc;
}
static int app_store(AppSlot *ctx, u32 va, const void *src, u32 n)
{
    (void)va; (void)src; (void)n;
    CHECK(ctx == &slots[is_user ? 2 : 1] && closed == 1 && failure == OK);
    event('V');
    __builtin_longjmp(escape, 1);
}

/* Unrelated admission/allocation and execution boundaries. */
static int paging_v86_session_open(void) { return 0; }
static int v86_call_pinned(void) { return 0; }
u32 paging_current_cr3(void) { return 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd) { CHECK(pd == 1); }
static u8 launch_read_byte(u32 pd, const char *p, int *failed) { (void)pd; (void)failed; return *p; }
void shell_print(const char *s, u8 a) { (void)s; (void)a; CHECK(0); }
void shell_print_dec(u32 n, u8 a) { (void)n; (void)a; CHECK(0); }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; CHECK(0); }
int appslot_cur(void) { return 1; }
AppSlot *appslot_at(int id) { return &slots[id]; }
int appslot_start_admit(int gui, u32 pages, u32 free_pages) { (void)gui; (void)pages; (void)free_pages; return 2; }
int appslot_cui_only_admit(int gui, u32 flags) { (void)gui; (void)flags; return 0; }
int os32x_layout_check(const OS32Header *h, u32 n, u32 off) { (void)h; (void)n; (void)off; return OS32X_LAYOUT_OK; }
const char *os32x_layout_reason(int rc) { (void)rc; CHECK(0); return 0; }
static const char *exec_image_reject_reason(const OS32Header *h, int shell, u32 base, u32 max) { (void)h; (void)shell; (void)base; (void)max; return 0; }
static int exec_stack_bytes(u32 requested, u32 len, u32 *size) { (void)requested; (void)len; *size = MEM_EXEC_STACK_SIZE; return 0; }
static int exec_shell_layout(u32 text, u32 bss, u32 heap, u32 *end, u32 *base, u32 *size) { (void)text; (void)bss; (void)heap; *end = MEM_SHELL_STACK_TOP; *base = *size = 0; return 0; }
static u32 exec_ring3_pages(u32 base, u32 end, u32 heap, u32 stack) { (void)base; (void)end; (void)heap; (void)stack; return 10; }
u32 pgalloc_free_pages(void) { return 100; }
static void exec_restore_band(int id) { (void)id; CHECK(0); }
static void exec_restore_context(int id) { (void)id; CHECK(0); }
static void exec_heap_save_state(u32 *used) { *used = 0; }
static int exec_disk_write_path_allowed(const char *s) { (void)s; return 0; }
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out) { (void)kind; (void)id; (void)name; *out = 9; return 1; }
int ledger_owner_retire(u32 owner) { (void)owner; CHECK(0); return 0; }
static void *kmalloc(u32 n) { CHECK(n == sizeof(space)); return &space; }
static void kfree(void *p) { (void)p; CHECK(0); }
int paging_addrspace_create_lease(struct addrspace *as, u32 owner) { CHECK(as == &space && owner == 9); as->pd_phys = 23; return 0; }
static void appmem_init(struct addrspace *as, u32 b, u32 e, u32 h, u32 g) { (void)as; (void)b; (void)e; (void)h; (void)g; }
int paging_addrspace_clear_app_band(struct addrspace *as) { CHECK(as == &space); return 0; }
static int app_map_region(struct addrspace *as, u32 begin, u32 end) { CHECK(as == &space); (void)begin; (void)end; return 0; }
int shlib_addrspace_attach(struct addrspace *as) { CHECK(as == &space); return 0; }
static void ring3_context_save(volatile Ring3CallContext *c) { *c = 1; }
static void ring3_context_clear(void) { CHECK(0); }
static void ring3_context_restore(volatile Ring3CallContext *c) { (void)c; CHECK(0); }
int exec_setjmp(u32 *buf) { (void)buf; CHECK(!jump_ready); jump_ready = 1; event('J'); return 0; }
static void exec_pending_finish(void) { CHECK(0); }
static void _enable(void) { CHECK(0); }
static void exec_heap_user_init(struct addrspace *as) { (void)as; CHECK(0); }
static void exec_heap_init_at(u32 base, u32 size) { (void)base; (void)size; CHECK(0); }
static void kselftest_run_audit(const char *s) { (void)s; CHECK(0); }
void appslot_shell_commit(void) { CHECK(0); }
static void shell_service_retire(void) { CHECK(0); }
static void shell_service_publish(u32 base, u32 size) { (void)base; (void)size; CHECK(0); }
void appslot_start_commit(int id, int gui, u32 pages) { (void)id; (void)gui; (void)pages; CHECK(0); }
static void exec_owner_diag(AppSlot *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; CHECK(0); }
void appslot_mark_scheduled(int id, u32 tick) { (void)id; (void)tick; CHECK(0); }
#undef arch_enter_user
#undef arch_call_on_stack
#define arch_enter_user(pd, sp, entry) do { (void)(pd); (void)(sp); (void)(entry); CHECK(0); } while (0)
#define arch_call_on_stack(saved, sp, entry) do { (saved) = 0; (void)(saved); (void)(sp); (void)(entry); CHECK(0); } while (0)
void exec_exit(int code, int kind) { (void)code; (void)kind; CHECK(0); }

#include "exec_image_source.inc"

static void check_case(int user, int fault, u32 text)
{
    is_user = user; failure = fault; image_size = text; bss_size = 33;
    opened = closed = aborted = reads = maps = jump_ready = 0;
    copied = event_count = 0;
    kmemset(events, 0, sizeof(events));
    kmemset(slots, 0, sizeof(slots));
    kmemset(backing, 0xA5, sizeof(backing));
    kmemset((void *)MEM_SHELL_LOAD_ADDR, 0xA5, 3 * PAGE_SIZE);
    exec_nest_level = user;
    int reached_argv = __builtin_setjmp(escape);
    if (!reached_argv) {
        int rc = exec_launch("/fixture", 0);
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
void _start(void)
{
    u32 result, args[6] = {MEM_SHELL_LOAD_ADDR, 3 * PAGE_SIZE, 3, 0x32, 0xffffffffu, 0};
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_SHELL_LOAD_ADDR);
    for (int user = 0; user <= 1; user++) {
        for (int fault = OK; fault <= READ_ERROR; fault++) {
            if (!user && (fault == MAP_FIRST || fault == MAP_LAST)) continue;
            check_case(user, fault, PAGE_SIZE + 17);
        }
        check_case(user, OK, PAGE_SIZE);
        check_case(user, OK, 1);
        check_case(user, OK, 0);
    }
    say("PASS exec image: 20 launch cases, bytes, page boundaries, BSS and close-before-abort\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
