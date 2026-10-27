/* T7a-2e: real paging/session/BIOS with a qemu-user low VA mapping.
 * CPU CR3/TLB and exception delivery are explicit substitutes; no signal stack
 * is claimed to emulate #PF. The low fixture executes actual x86 instructions.
 */
#include "types.h"
static u32 host_cr3;
static void flush_low(void);
#include "v86.h"
#include "v86_call.h"
#include "idt.h"
#include "tvram.h"
#include "paging.c"
#include "pgalloc.c"
#include "pgalloc_host_fixture.h"
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
static u32 corrupt_restore_pde;
static u32 tlb[MEM_1MB / PAGE_SIZE], tlb_pde, tlb_root, flushes;
static void flush_low(void) {
    flushes++;
    if (corrupt_restore_pde) page_directory[0] ^= PTE_USER;
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
    static const char expected[][65] = {
        "V86 RESTORE: bad=00000100 PTE=00000100 PDE=00000000 PCD=00000000",
        "V86 RESTORE: bad=00000001 PTE=00000000 PDE=00000001 PCD=00000000",
        "V86 RESTORE: bad=00000001 PTE=00000000 PDE=00000000 PCD=00000001",
    };
    for (u32 col = 0; col < TVRAM_COLS; col++) {
        CHECK(((volatile u16 *)P2V_IO(TVRAM_BASE))[col] ==
              (col < sizeof(expected[0]) - 1 ? (u8)expected[expect_stop - 1][col] : ' ') &&
              ((volatile u16 *)P2V_IO(TVRAM_ATTR))[col] == TATTR_RED,
              "restore TVRAM diagnostic");
    }
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
#include "clock.c"
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
static u32 allocation_calls;
static u32 checked_alloc(u32 owner, int pages) __attribute__((unused));
static u32 checked_alloc(u32 owner, int pages) {
    allocation_calls++;
    return pgalloc_alloc_phys(owner, pages);
}
#define pgalloc_alloc_phys checked_alloc
#define paging_v86_snapshot checked_snapshot
#define paging_v86_map_range checked_map
#define v86_bios_setup checked_bios
#define _stop restore_stop
#include "v86_mem.c"
#undef _stop
#undef v86_bios_setup
#undef paging_v86_map_range
#undef paging_v86_snapshot
#undef pgalloc_alloc_phys
struct kdebug kdebug;
volatile u32 kctx_irq_depth, kctx_exc_depth;
static u32 releases;
void v86_io_apply_policy(void) { boundary(); }
void v86_io_reset_policy(void) {}
void v86_runtime_end(void) { v86_session.active = v86_session.running = 0; }
void gfx_v86_return(void) { CHECK(!kctx_exc_depth && !v86_session.open, "normal release"); releases++; }
void kselftest_audit_v86_return(void) {}
void loop_dev_detach(int slot) { (void)slot; }
static u32 saved_pt[MEM_1MB / PAGE_SIZE], saved_pde, caller_pd[PDE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static u8 original(u32 a) { return (u8)(a ^ (a >> 8) ^ 0x39); }
static u8 mailbox_byte(u32 a) { return (u8)(a * 17 + 3); }
static u32 low_region_count, pool_free;
static struct ledger_region low_regions[LEDGER_MAX_REGIONS];
static void reservation_unchanged(void) {
    CHECK(ledger_region_count == low_region_count, "reservation count unchanged");
    for (u32 i = 0; i < sizeof(low_regions); i++)
        CHECK(((u8 *)ledger_regions)[i] == ((u8 *)low_regions)[i], "reservation bytes unchanged");
    CHECK(pgalloc_free_pages() == pool_free, "session pool unchanged");
    CHECK(!allocation_calls, "zero session backing allocations");
    for (u32 p = 0; p < 160; p++) {
        u32 hits = 0;
        CHECK(!bit(eligible, p), "low pool supply zero");
        for (u32 i = 0; i < ledger_region_count; i++) {
            const struct ledger_region *r = &ledger_regions[i];
            if (p >= r->first && p < r->end) {
                CHECK(r->first == 0 && r->end == 160 && r->type == LEDGER_R_FIXED &&
                      r->owner == LEDGER_OWNER_KERNEL && r->cache == LEDGER_CACHE_WB,
                      "single FIXED kernel WB all 160 PFNs");
                hits++;
            }
        }
        CHECK(hits == 1, "one low reservation");
    }
}
static void restored(void) {
    reservation_unchanged();
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
    reservation_unchanged();
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
    host_pool_boot(16384);
#include "low_reservation.inc"
    for (u32 i = 0; i < sizeof(memory_boot_fixed) / sizeof(memory_boot_fixed[0]); i++) {
        CHECK(ledger_register_region(memory_boot_fixed[i].type, memory_boot_fixed[i].owner,
              memory_boot_fixed[i].first, memory_boot_fixed[i].end,
              memory_boot_fixed[i].cache, LEDGER_RF_PERMANENT), "boot low reservation registration");
    }
    low_region_count = ledger_region_count;
    kmemcpy(low_regions, ledger_regions, sizeof(low_regions));
    pool_free = pgalloc_free_pages();
    reservation_unchanged();
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
    /* Exhaust the real contiguous pool after module/staging preparation.
     * The direct product path still runs, and repeated sessions leak no pages.
     * Actual GUI transitions remain a guest acceptance (gfx is stubbed here). */
    u32 held[4096], held_n = 0, p;
    while ((p = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 1))) held[held_n++] = p;
    CHECK(!pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 159), "contiguous pool exhausted");
    pool_free = pgalloc_free_pages();
    CHECK(!pool_free, "pool completely exhausted");
    for (u32 cycle = 0; cycle < 20; cycle++) {
        before();
        CHECK(!v86_session_begin(LEDGER_OWNER_KERNEL), "V86 available with exhausted pool");
        execute_low(); v86_session_end(); restored();
        CHECK(!module_refs && !v86_session.release_pending, "20 sessions no pin leak");
    }
    while (held_n) CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,
                       held[--held_n] / PAGE_SIZE, 1), "release exhaustion fixture");
    pool_free = pgalloc_free_pages();
    /* A failed restore must never reach caller copyout/unpin. */
    for (u32 kind = 1; kind <= 3; kind++) {
        before(); CHECK(!v86_session_begin(2), "fail-stop begin");
        expect_stop = kind;
        if (kind == 1) v86_map_session = 0;
        if (kind == 2) corrupt_restore_pde = 1;
        if (kind == 3) v86_session.low_pte[V86_VRAM_START / PAGE_SIZE] ^= PTE_PCD;
        kmemset(P2V_IO(TVRAM_BASE), 0, TVRAM_BPR);
        if (!__builtin_setjmp(stop_jmp)) {
            v86_session_end(); CHECK(0, "restore error fail-stop");
        }
        CHECK(module_refs && v86_session.open, "fail-stop retains pin");
        /* Only the fixture resumes a fail-stop, to exercise each diagnosis. */
        corrupt_restore_pde = v86_restore_mismatch = v86_session.closing = 0;
        v86_map_session = &v86_session;
        kmemcpy(v86_session.low_pte, saved_pt, sizeof(saved_pt));
        v86_session_end();
        restored();
    }
    SAY("GREEN single FIXED/kernel/WB 160 PFNs / 20 exhausted-pool sessions / backing alloc=0 (-636KiB) / direct 640KiB execution / page0 NP / mailbox / prepare failures / exits / TLB / fail-stop");
    die(0);
}
