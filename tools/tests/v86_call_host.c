/* Actual entry and T2 checked copies. CPU root loads, slots and VA->PA walks
 * are explicit substitutes; this does not simulate V86/IRQ hardware entry. */
#include "types.h"
#include "vfs.h"
#include "appslot.h"
#include "redir_access.h"
#include "v86_gcap.h"
#include "v86_mem.h"
#include "os32_kapi_slots.h"
#define IO_H
static u32 host_if = 1;
static u32 irq_save(void) { u32 f = host_if; host_if = 0; return f; }
static void irq_restore(u32 f) { host_if = f; }
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void say(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL " #x); die(1); } } while (0)
static AppSlot slots[APP_SLOT_COUNT];
static struct addrspace as;
static u32 root = 0x2000, live_count = 1, gui, calls, ptes;
volatile int ring3_in_syscall = 1, ring3_wm_depth;
struct v86_session_state v86_session;
static int fail_alloc, allocations, frees, fail_body;
static u8 heap[sizeof(V86Gcap) + VFS_MAX_PATH * 2 + 2048];
static u32 heap_used;
/* Caller VA is deliberately different from the kernel staging/PA. */
#define USER_BASE MEM_EXEC_LOAD_ADDR
static u8 user[PAGE_SIZE * 3] __attribute__((aligned(PAGE_SIZE)));
static int reject_second, readonly, entry_fault;
static u32 admission_notice;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{ (void)attr; (void)fmt; admission_notice++; }
AppSlot *appslot_get(int id)
{
    return id > 0 && id < APP_SLOT_COUNT && slots[id].state ? &slots[id] : 0;
}
int appslot_cur(void) { return 2; }
int res_owner_get(void) { return 2; }
int ring3_call_from_user(void) { return ring3_in_syscall && !ring3_wm_depth; }
u32 paging_kernel_pd_phys(void) { return 0x1000; }
u32 paging_current_cr3(void) { return root; }
void paging_load_cr3(u32 pd) { CHECK(host_if); root = pd; }
u32 paging_live_addrspaces(void) { return live_count; }
int con_sink_is_enabled(void) { return gui; }
int v86_gui_refuse(void) { CHECK(gui); return 1; }
u32 tss_get_esp0(void) { return MEM_KSTACK_TOP; }
void *kmemcpy(void *d, const void *s, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
void *kmalloc(u32 n)
{
    if (fail_alloc) return 0;
    CHECK(heap_used + n <= sizeof(heap));
    void *p = heap + heap_used;
    heap_used += (n + 7) & ~7U;
    allocations++;
    return p;
}
void *kzalloc(u32 n)
{
    void *p = kmalloc(n);
    if (p) for (u32 i = 0; i < n; i++) ((u8 *)p)[i] = 0;
    return p;
}
void kfree(void *p) { CHECK(p); frees++; }
int as_access_page(const struct addrspace *a, u32 va, int write, u32 *pa)
{
    CHECK(root == as.pd_phys); /* No caller access under master. */
    if (a != &as || va < USER_BASE || va - USER_BASE >= sizeof(user) ||
        (reject_second && va - USER_BASE >= PAGE_SIZE) ||
        (write && readonly)) return 0;
    *pa = (u32)(uptr)(user + va - USER_BASE);
    return 1;
}
#include "redir_source.c"
#include "v86_call_source.c"

static u32 invoke(const u32 *args, u32 n)
{
    CHECK(v86_call_pinned() && root == paging_kernel_pd_phys());
    CHECK(v86_call_admit());
    CHECK(call.caller.generation == as.generation && call.cr3 == as.pd_phys);
    if (n) {
        CHECK(args[0] < MEM_APP_BAND_BASE);
        CHECK(((const char *)(uptr)args[0])[0] == 'a' ||
              ((const char *)(uptr)args[0])[0] == 'x');
    }
    if (n == 8 && args[1]) {
        CHECK(args[1] < MEM_APP_BAND_BASE);
        CHECK(((const char *)(uptr)args[1])[0] == 'b');
    }
    u32 result;
    CHECK(v86_call_dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0, &result));
    CHECK((int)result == OS32_ERR_BUSY);
    calls++; ptes++; /* Simulated session's first low-PTE mutation. */
    return fail_body ? (u32)-7 : 0;
}
u32 kapi_invoke(void *fn, const void *args, u32 n)
{
    (void)fn;
    /* The existing disk APIs accept NULL at the wrapper and return an error
     * from the body; this is not a caller-memory fault. */
    if (n && !((const u32 *)args)[0]) return (u32)-1;
    return invoke(args, n);
}
int v86_gdc_capture(int mode, V86Gcap *out)
{
    if (mode != V86G_MODE_SELFTEST && mode != V86G_MODE_ROM) return OS32_ERR_INVAL;
    invoke(0, 0);
    CHECK(out == call.capture && (uptr)out < MEM_APP_BAND_BASE);
    out->mode = 0x1234;
    return fail_body ? -7 : 0;
}
static u32 dispatch(u32 slot, u32 a, u32 b, u32 n)
{
    u32 args[2] = {a, b}, result;
    kmemcpy(user + 512, args, sizeof(args));
    int rc = v86_call_dispatch(slot, 0, (void *)(USER_BASE + 512), n, &result);
    CHECK(rc);
    entry_fault = rc < 0;
    CHECK(root == as.pd_phys && host_if && !v86_call_pinned());
    CHECK(allocations - frees == 1); /* Only boot mailbox remains. */
    heap_used = 872;
    return result;
}
void _start(void)
{
    slots[2].state = APP_STATE_RUNNING;
    slots[2].cpl3 = 1; slots[2].parent = APP_ID_SHELL; slots[2].as = &as;
    as.pd_phys = root; as.generation = 17; as.owner = 9;
    CallerAccessFrame previous;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    fail_alloc = 1;
    CHECK(!v86_call_init());
    u32 r;
    CHECK(v86_call_dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0, &r));
    CHECK((int)r == OS32_ERR_NOSPC && !ptes && !v86_call_pinned());
    fail_alloc = 0;
    CHECK(v86_call_init() && v86_call_init() && allocations == 1);
    CHECK(heap_used == 872);
    user[0] = 'a'; user[1] = 0; user[PAGE_SIZE] = 'b';
    CHECK(!dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0));
    CHECK(!dispatch(KAPI_SLOT_V86_BOOT2, USER_BASE, USER_BASE + PAGE_SIZE, 8));
    CHECK(!dispatch(KAPI_SLOT_V86_DISKTEST, USER_BASE, 0, 4));
    fail_body = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT, USER_BASE, 0, 4) == -7);
    fail_body = 0;
    CHECK((int)dispatch(KAPI_SLOT_V86_DISKTEST, 0, 0, 4) == -1 && !entry_fault);
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT, 0, 0, 4) == -1 && !entry_fault);
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT2, 0, 0, 8) == -1 && !entry_fault);
    CHECK(!dispatch(KAPI_SLOT_V86_BOOT2, USER_BASE, 0, 8));
    fail_alloc = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT, USER_BASE, 0, 4) == OS32_ERR_NOSPC);
    CHECK((int)dispatch(KAPI_SLOT_V86_GDC_CAPTURE, V86G_MODE_SELFTEST,
                         USER_BASE, 8) == OS32_ERR_NOSPC);
    fail_alloc = 0;
    u32 before = ptes;
    gui = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == -1);
    gui = 0; ring3_wm_depth = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == OS32_ERR_BUSY);
    ring3_wm_depth = 0; slots[2].parent = 3;
    u32 notice_before = admission_notice;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == OS32_ERR_BUSY);
    CHECK(admission_notice == notice_before + 1);
    slots[2].parent = APP_ID_SHELL; live_count = 2;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == OS32_ERR_BUSY);
    live_count = 1; slots[3].state = APP_STATE_PARKED;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == OS32_ERR_BUSY);
    slots[3].state = 0; v86_session.open = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_SELFTEST, 0, 0, 0) == OS32_ERR_BUSY);
    v86_session.open = 0;
    for (u32 i = 0; i < VFS_MAX_PATH; i++) user[i] = 'x';
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT, USER_BASE, 0, 4) == OS32_ERR_INVAL);
    CHECK(entry_fault);
    user[0] = 'a'; user[1] = 0; reject_second = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_BOOT2, USER_BASE, USER_BASE + PAGE_SIZE, 8) == OS32_ERR_INVAL);
    CHECK(entry_fault);
    reject_second = 0; readonly = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_GDC_CAPTURE, V86G_MODE_SELFTEST, USER_BASE, 8) == OS32_ERR_INVAL);
    CHECK(entry_fault);
    readonly = 0;
    CHECK((int)dispatch(KAPI_SLOT_V86_GDC_CAPTURE, V86G_MODE_SELFTEST, 0, 8) == OS32_ERR_INVAL);
    CHECK(!entry_fault);
    ((V86Gcap *)user)->mode = 0x8765;
    CHECK((int)dispatch(KAPI_SLOT_V86_GDC_CAPTURE, 99, USER_BASE, 8) == OS32_ERR_INVAL);
    CHECK(((V86Gcap *)user)->mode == 0x8765 && !entry_fault);
    CHECK(ptes == before);
    CHECK(!dispatch(KAPI_SLOT_V86_GDC_CAPTURE, V86G_MODE_SELFTEST, USER_BASE, 8));
    CHECK(((V86Gcap *)user)->mode == 0x1234);
    ((V86Gcap *)user)->mode = 0x5678;
    fail_body = 1;
    CHECK((int)dispatch(KAPI_SLOT_V86_GDC_CAPTURE, V86G_MODE_SELFTEST, USER_BASE, 8) == -7);
    CHECK(((V86Gcap *)user)->mode == 0x5678);
    /* Trusted landing after a fault restores before abandoning staging. */
    call.pinned = 1; call.cr3 = as.pd_phys; root = paging_kernel_pd_phys();
    call.path[0] = kmalloc(16); call.capture = kzalloc(16);
    v86_call_cleanup();
    CHECK(!call.pinned && root == as.pd_phys && allocations - frees == 1);
    SAY("GREEN V86 entry/copy/pin/master/cleanup");
    die(0);
}
