/* PowerMac HW_OP design §5, 5c. Model effective page translations separately
 * from the raw leaves. Count accesses before and after injected failures. */
#include "hwb_window.h"
#include "ppc_trap.h"
#include "mm/paging.h"
#include "kdebug/hwb_wire.h"

static u32 leaves[2], effective[2], irq = PPC_MSR_EE;
static int stale_restore;
static u32 writes, calls, stores, fail_at, order[8], guard, phys[4];
static void quit(u32 rc) __attribute__((noreturn));
static void quit(u32 rc)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) quit(__LINE__); } while (0)
void arch_cpu_stop(void) { quit(127); }
u32 ppc_root_current(void) { return 0x1000; }
u32 ppc_irq_save(void) { u32 saved = irq; irq = 0; return saved; }
void ppc_irq_restore(u32 flags) { irq = flags; }
int paging_window_reserve(void) { REQUIRE(!irq); return 0; }
int paging_window_release(void) { quit(126); }
u32 paging_pte_raw(u32 va) { return leaves[(va - PPC_HWB_WINDOW_VA) >> 12]; }
int paging_pte_restore(u32 va, u32 raw)
{
    u32 i = (va - PPC_HWB_WINDOW_VA) >> 12;
    REQUIRE(!irq && i < 2 && writes < 8);
    order[writes++] = i;
    leaves[i] = raw;
    if (writes == fail_at) return -1;
    if (writes == 3 && stale_restore && fail_at == 0) return 0;
    effective[i] = raw;
    return 0;
}
int ppc_pd_lookup_read(u32 root, u32 va, int store, int user,
                       struct ppc_translation *t, ppc_pd_read_fn fn, void *ctx)
{
    (void)root; (void)store; (void)user; (void)fn; (void)ctx;
    u32 raw = effective[(va - PPC_HWB_WINDOW_VA) >> 12];
    t->pa = raw & PTE_FRAME_MASK;
    t->attr = 0x28 | (raw & PTE_RW ? 0 : 3);
    return raw & PTE_PRESENT;
}
int ppc_trap_onfault_transfer(u32 pc, u32 start, u32 end, u32 width, int store, u32 resume)
{
    REQUIRE(pc && resume && !irq && end >= start && width == 4 && store);
    if (guard) return 0;
    guard = 1;
    return 1;
}
void ppc_trap_onfault_clear(void) { REQUIRE(guard); guard = 0; }
static int transfer(u32 va, u32 value, u32 count, u32 increment)
{
    REQUIRE(!irq && guard && va == PPC_HWB_WINDOW_VA + 4092);
    calls++;
    for (u32 i = 0; i < count; ++i) { phys[i] = value; stores++; value += increment; }
    return 0;
}
#define LABELS(n) char ppc_hwb_##n##_insn[4], ppc_hwb_##n##_resume[4]; \
    int ppc_hwb_##n(u32 v,u32 a,u32 c,u32 d) { return transfer(v,a,c,d); }
LABELS(read8) LABELS(read16) LABELS(read16le) LABELS(read32) LABELS(read32le)
LABELS(write8) LABELS(write16) LABELS(write16le) LABELS(write32) LABELS(write32le)
void _start(void)
{
    u32 v = 15;
    pm_hwb_window_init();
    leaves[0] = effective[0] = 0x12345010;
    leaves[1] = effective[1] = 0xabcde011;
    fail_at = 2;
    REQUIRE(pm_hwb_window_transfer(0xaffc,4,0,1,3,1,&v) == HW_WINDOW);
    REQUIRE(!calls && !stores && irq == PPC_MSR_EE && writes == 4);
    REQUIRE(order[0]==0 && order[1]==1 && order[2]==1 && order[3]==0);
    REQUIRE(effective[0]==0x12345010 && effective[1]==0xabcde011);
    REQUIRE(leaves[0]==effective[0] && leaves[1]==effective[1]);
    writes = fail_at = 0;
    REQUIRE(pm_hwb_window_transfer(0xaffc,4,0,1,3,1,&v) == HW_OK);
    REQUIRE(calls==1 && stores==3 && phys[0]==15 && phys[1]==16 && phys[2]==17);
    writes = 0;
    stale_restore = STALE_RESTORE;
    fail_at = stale_restore ? 0 : 3;
    REQUIRE(pm_hwb_window_transfer(0xaffc,4,0,1,3,1,&v) == HW_WINDOW);
    REQUIRE(!leaves[0] && !leaves[1] && irq == PPC_MSR_EE);
    REQUIRE(writes==6 && order[2]==1 && order[3]==0 && !effective[0] && !effective[1]);
    REQUIRE(pm_hwb_window_transfer(0xaffc,4,0,1,3,1,&v) == HW_WINDOW);
    REQUIRE(calls==2 && stores==6 && writes==6 && irq == PPC_MSR_EE);
    quit(0);
}
