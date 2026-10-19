/* T7a-2b: real paging/session/BIOS with a qemu-user low VA mapping.
 * CPU CR3/TLB and exception delivery are explicit substitutes; no signal stack
 * is claimed to emulate #PF. The low fixture executes actual x86 instructions.
 */
#include "types.h"
#include "v86.h"
#include "v86_call.h"
#include "idt.h"
static u32 host_cr3;
static void flush_low(void);
#include "paging.c"
#include "module.h"
#include "kstring.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s, u32 n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
#define CHECK(c, s) do { if (!(c)) { SAY("FAIL " s); die(1); } } while (0)
static u32 tlb[MEM_1MB / PAGE_SIZE], tlb_pde, tlb_root, flushes;
static void flush_low(void) {
    flushes++;
    tlb_root = host_cr3;
    if (!page_tables[0]) return;
    tlb_pde = ((u32 *)P2V(host_cr3))[0];
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) tlb[i] = page_tables[0][i];
}
static u32 fail_map, maps, module_refs, refuse_module, snapshot_fail;
static u32 boundary_fail, boundaries, abort_jmp[5], stop_jmp[5], expect_stop;
static u8 mailbox_storage[MEM_AUTOPLAY_MAILBOX_SIZE];
static int missing_mailbox;
void *v86_call_mailbox(void) { return missing_mailbox ? 0 : mailbox_storage; }
static void boundary(void) {
    if (++boundaries == boundary_fail) __builtin_longjmp(abort_jmp, 1);
}
static void __attribute__((unused)) restore_stop(void) {
    CHECK(expect_stop, "restore must complete before caller return");
    CHECK(v86_restore_mismatch, "restore mismatch counted");
    __builtin_longjmp(stop_jmp, 1);
}
void *kmemcpy(void *d, const void *s, u32 n) {
    volatile u8 *dst = d; const volatile u8 *src = s;
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
    return d;
}
void *kmemset(void *d, int c, u32 n) {
    volatile u8 *dst = d;
    for (u32 i = 0; i < n; i++) dst[i] = (u8)c;
    return d;
}
int v86_module_session_acquire(void) {
    if (refuse_module) return 0;
    module_refs++;
    return 1;
}
void v86_module_session_release(void) { CHECK(module_refs == 1, "module pin"); module_refs--; }
static int checked_snapshot(struct v86_session_state *s) {
    return snapshot_fail ? -1 : paging_v86_snapshot(s);
}
static int checked_map(u32 base, u32 end, u32 phys, u32 flags) {
    if (++maps == fail_map) return -1;
    return paging_v86_map_range(base, end, phys, flags);
}
#include "v86_bios.c"
#include "v86_snapshot.c"
#include "sysclk.c"
#include "bios_consumer_slice.inc"
static void checked_bios(void) {
    /* Whole conventional RAM except restored page0 must be freshly zero. */
    for (u32 a = PAGE_SIZE; a < MEM_CONV_END; a++)
        CHECK(*(volatile u8 *)P2V(a) == 0, "previous guest data zero");
    CHECK(*(volatile u32 *)P2V(V86_TEST_MAGIC_ADDR) == 0, "test word zero");
    boundary(); /* zero complete, before BIOS edits */
    v86_bios_setup();
    boundary(); /* BIOS complete, before I/O policy */
}
#define OS32_V86_DIRECT_LOW_FIXTURE 1
#define paging_v86_snapshot checked_snapshot
#define paging_v86_map_range checked_map
#define v86_bios_setup checked_bios
#define _stop restore_stop
#include "v86_mem.c"
#undef _stop
#undef v86_bios_setup
#undef paging_v86_map_range
#undef paging_v86_snapshot
struct kdebug kdebug;
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
volatile u32 kctx_irq_depth, kctx_exc_depth;
static u32 releases;
void v86_io_apply_policy(void) { boundary(); }
void v86_io_reset_policy(void) {}
void v86_runtime_end(void) { v86_session.active = v86_session.running = 0; }
void gfx_v86_return(void) { CHECK(!kctx_exc_depth && !v86_session.open, "normal release"); releases++; }
void kselftest_audit_v86_return(void) {}
void loop_dev_detach(int slot) { (void)slot; }
u32 pgalloc_alloc_pt(void) { return 0; }
void pgalloc_free_pt(u32 phys) { (void)phys; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n) {
    (void)owner; (void)pfn; (void)n;
    CHECK(0, "direct fixture has no backing allocation"); return 0;
}
static u32 saved_pt[MEM_1MB / PAGE_SIZE], saved_pde, caller_pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u8 original(u32 a) { return (u8)(a ^ (a >> 8) ^ 0x39); }
static u8 mailbox_byte(u32 a) { return (u8)(a * 17 + 3); }
static void restored(void) {
    CHECK(!v86_session.open && !paging_v86_session_open(), "session closed");
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) {
        CHECK(page_tables[0][i] == saved_pt[i], "exact PTE including last ROM page");
        CHECK(tlb[i] == saved_pt[i], "TLB restored before caller");
    }
    CHECK(page_directory[0] == saved_pde && tlb_pde == saved_pde &&
          tlb_root == paging_kernel_pd_phys(), "master PDE CR3 TLB restored");
    CHECK(!(page_tables[0][0] & PTE_PRESENT), "normal page0 NP");
    for (u32 i = 0; i < PAGE_SIZE; i++) {
        CHECK(*(volatile u8 *)P2V(i) == original(i), "page0 restored all 4096 bytes");
        CHECK(v86_session.real_lowmem[i] == original(i), "immutable boot original");
    }
    for (u32 i = 0; i < MEM_AUTOPLAY_MAILBOX_SIZE; i++)
        CHECK(*(volatile u8 *)P2V(MEM_AUTOPLAY_MAILBOX_BASE + i) == mailbox_byte(i), "mailbox restored");
    paging_load_cr3(V2P(caller_pd));
    flush_low(); /* CR3 reload is a TLB flush on the CPU. */
    CHECK(tlb_root == V2P(caller_pd) && tlb_pde == caller_pd[0], "caller CR3 restored");
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++)
        CHECK(tlb[i] == saved_pt[i], "caller shared PT restored");
    paging_load_cr3(paging_kernel_pd_phys());
    flush_low();
}
static void before(void) {
    maps = boundaries = 0;
    for (u32 a = PAGE_SIZE; a < MEM_CONV_END; a++) *(volatile u8 *)P2V(a) = 0xa7;
    for (u32 i = 0; i < MEM_AUTOPLAY_MAILBOX_SIZE; i++)
        *(volatile u8 *)P2V(MEM_AUTOPLAY_MAILBOX_BASE + i) = mailbox_byte(i);
}
static void execute_low(void) {
    for (u32 a = 0; a < MEM_CONV_END; a += PAGE_SIZE) {
        CHECK((tlb[a / PAGE_SIZE] & ~(PAGE_SIZE - 1)) == a, "direct physical PFN");
        CHECK((tlb[a / PAGE_SIZE] & (PAGE_RW | PTE_USER)) == (PAGE_RW | PTE_USER), "direct guest RW USER");
    }
    CHECK(tlb_pde & PTE_USER, "guest PDE USER");
    CHECK((*(volatile u8 *)P2V(0x501) & 7) == V86_GUEST_MEM_CODE, "BIOS 640KiB");
    /* mov byte [edi],5a; mov byte [esi],a5; ret. Executed inside direct low
     * RAM, writing the last conventional byte and the 0x8C000 test word.
     * vm86/IRET and actual hardware TLB operation remain NP21/W acceptance. */
    static const u8 code[] = {0xc6, 0x07, 0x5a, 0xc6, 0x06, 0xa5, 0xc3};
    kmemcpy(P2V(V86_TEST_CODE_ADDR), code, sizeof(code));
    void (*entry)(void) = P2V(V86_TEST_CODE_ADDR);
    __asm__ volatile("call *%0" : : "r"(entry), "D"(MEM_CONV_END - 1), "S"(V86_TEST_MAGIC_ADDR) : "memory");
    CHECK(*(volatile u8 *)P2V(MEM_CONV_END - 1) == 0x5a &&
          *(volatile u8 *)P2V(V86_TEST_MAGIC_ADDR) == 0xa5, "execute direct low fixture");
    kmemset(P2V(0), 0xcc, PAGE_SIZE);
    sysclk_detect();
    CHECK(sysclk_is_8mhz() == !!(original(BIOS_WORK_SYSCLK) & BIOS_SYSCLK_8MHZ), "clock boot original");
    CHECK(kbd_bios_shift_peek() == original(BIOS_WORK_KB_SHIFT), "keyboard boot original");
    kmemset(P2V(MEM_AUTOPLAY_MAILBOX_BASE), 0xdd, MEM_AUTOPLAY_MAILBOX_SIZE);
}
void _start(void) {
    u32 args[6] = {0, 16 * 1024 * 1024, 7, 0x32, ~0U, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(!result, "host low address mapping (qemu -B required)");
    host_map_fixed_paging();
    for (u32 i = 0; i < PAGE_SIZE; i++) *(volatile u8 *)P2V(i) = original(i);
    v86_bios_save_real();
    paging_init(16384);
    paging_reclaim_conventional();
    CHECK(!(page_tables[0][0] & PTE_PRESENT) && memmap_want_at(0, 0) == MM_NP, "normal page0 NP");
    /* Shared SHM/trampoline PDE USER must survive all exits, including failure
     * before the final ROM mapping (last saved PTE is deliberately nontrivial). */
    page_directory[0] |= PTE_USER;
    for (u32 i = 0; i < PDE_COUNT; i++) caller_pd[i] = page_directory[i];
    live_addrspaces = 1;
    saved_pde = page_directory[0];
    for (u32 i = 0; i < MEM_1MB / PAGE_SIZE; i++) saved_pt[i] = page_tables[0][i];
    flush_low();
    before();
    missing_mailbox = 1;
    CHECK(v86_session_begin(2) == -2 && !module_refs, "mailbox unavailable before writes");
    missing_mailbox = 0;
    refuse_module = 1;
    CHECK(v86_session_begin(2) == -1 && !module_refs, "module unavailable before writes");
    refuse_module = 0;
    snapshot_fail = 1;
    CHECK(v86_session_begin(2) == -1 && !module_refs, "snapshot failure releases pin");
    snapshot_fail = 0;
    restored();
    for (u32 n = 1; n <= V86_IDENT_MAP_N + 2; n++) {
        before(); fail_map = n;
        CHECK(v86_session_begin(2) == -3 && !module_refs, "every preparation map failure");
        restored();
    }
    fail_map = 0;
    for (u32 n = 1; n <= 3; n++) {
        before(); boundary_fail = n;
        if (!__builtin_setjmp(abort_jmp)) {
            v86_session_begin(2);
            CHECK(0, "preparation abort reached");
        }
        v86_session_end(); restored();
        CHECK(!module_refs, "preparation abort releases pin");
    }
    boundary_fail = 0;
    /* Normal, #PF, #GP, STOP share the mandatory RESTORING path. Exception
     * unwind is separately exercised by the real exec paths in shm_user. */
    for (u32 exit = 0; exit < 4; exit++) {
        before();
        CHECK(!v86_session_begin(2), "direct begin");
        execute_low();
        if (exit) { v86_session.aborting = 1; kctx_exc_depth = 1; }
        v86_session_end();
        restored();
        if (exit) {
            CHECK(v86_session.release_pending && module_refs, "exception retains module");
            kctx_exc_depth = 0; v86_session_end();
        }
        CHECK(!module_refs && !v86_session.release_pending, "all exits release");
    }
    /* A failed restore must never reach caller copyout/unpin. */
    before(); CHECK(!v86_session_begin(2), "fail-stop begin");
    v86_map_session = 0; expect_stop = 1;
    if (!__builtin_setjmp(stop_jmp)) {
        v86_session_end(); CHECK(0, "restore error fail-stop");
    }
    CHECK(module_refs && v86_session.open, "fail-stop retains pin");
    SAY("GREEN direct 640KiB execution / page0 NP / mailbox / prepare failures / exits / TLB / fail-stop");
    die(0);
}
