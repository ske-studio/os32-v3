/* PowerMac HW_OP design §5, stages 5c/6a. This private entry takes a
 * resource-validated PA. Caller supplies a kernel-local value. */
#include "hwb_window.h"
#include "ppc_trap.h"
#include "ppc_hpt.h"
#include "mm/paging.h"
#include "kdebug/hwb_wire.h"

struct transfer {
    int (*run)(u32, u32, u32, u32);
    const char *pc, *resume;
};
#define DECLARE(name) \
    extern int ppc_hwb_##name(u32, u32, u32, u32); \
    extern char ppc_hwb_##name##_insn[], ppc_hwb_##name##_resume[]
DECLARE(read8);
DECLARE(read16);
DECLARE(read16le);
DECLARE(read32);
DECLARE(read32le);
DECLARE(write8);
DECLARE(write16);
DECLARE(write16le);
DECLARE(write32);
DECLARE(write32le);
#define ENTRY(name) {ppc_hwb_##name, ppc_hwb_##name##_insn, ppc_hwb_##name##_resume}
static const struct transfer transfers[2][3][2] = {
    {{ENTRY(read8), ENTRY(read8)}, {ENTRY(read16), ENTRY(read16le)},
     {ENTRY(read32), ENTRY(read32le)}},
    {{ENTRY(write8), ENTRY(write8)}, {ENTRY(write16), ENTRY(write16le)},
     {ENTRY(write32), ENTRY(write32le)}}
};

static int available, busy;
static int matches(u32 va, u32 raw);

void pm_hwb_window_init(void)
{
    u32 irq = ppc_irq_save();
    if (!available && !paging_window_reserve()) {
        if (matches(PPC_HWB_WINDOW_VA, 0) &&
            matches(PPC_HWB_WINDOW_VA + PAGE_SIZE, 0)) available = 1;
        else if (paging_window_release()) arch_cpu_stop();
    }
    ppc_irq_restore(irq);
}

static u32 mapped_word(u32 pa, void *context)
{
    (void)context;
    return *(volatile u32 *)P2V(pa);
}

static int matches(u32 va, u32 raw)
{
    struct ppc_translation t;
    if (paging_pte_raw(va) != raw) return 0;
    int mapped = ppc_pd_lookup_read(ppc_root_current(), va, 0, 0, &t,
                                    mapped_word, 0);
    if (!(raw & PTE_PRESENT)) return !mapped;
    return mapped && t.pa == (raw & PTE_FRAME_MASK) &&
           t.attr == ((raw & PTE_PCD ? 0x28u : raw & PTE_PWT ? 0x40u : 0x10u) |
                      (raw & PTE_RW ? 0u : 3u));
}

static int install(u32 va, u32 raw)
{
    return !paging_pte_restore(va, raw) && matches(va, raw);
}

int pm_hwb_window_transfer(u32 pa, u32 width, int little, int store,
                           u32 count, u32 increment, u32 *value)
{
    u32 saved[2], used = 0, result = 0;
    int rc = HW_WINDOW;
    if (!value) return HW_ARG;
    u32 operand = store ? *value : (u32)&result;
    if (!store) *value = 0;
    if ((width != 1 && width != 2 && width != 4) || (pa & (width - 1)) ||
        !count || count > PAGE_SIZE / width || (!store && count != 1) ||
        count * width - 1 > ~pa) return HW_ARG;
    u32 offset = pa & ~PTE_FRAME_MASK;
    u32 pages = 1 + (offset + count * width - 1) / PAGE_SIZE;
    u32 irq = ppc_irq_save();
    if (!available || busy) goto end;
    busy = 1;
    while (used < pages) {
        u32 va = PPC_HWB_WINDOW_VA + used * PAGE_SIZE;
        u32 raw = ((pa & PTE_FRAME_MASK) + used * PAGE_SIZE) |
                  PTE_PRESENT | PTE_PCD | (store ? PTE_RW : 0);
        saved[used++] = paging_pte_raw(va);
        if (!install(va, raw)) goto cleanup;
    }
    const struct transfer *t = &transfers[!!store][width >> 1][!!little];
    u32 va = PPC_HWB_WINDOW_VA + offset;
    rc = HW_IO_ERROR;
    if (ppc_trap_onfault_transfer((u32)t->pc, va, va + (count - 1) * width,
                                  width, store, (u32)t->resume) &&
        !t->run(va, operand, count, increment)) {
        ppc_trap_onfault_clear();
        rc = HW_OK;
    }
cleanup:
    while (used) {
        --used;
        if (!install(PPC_HWB_WINDOW_VA + used * PAGE_SIZE, saved[used])) {
            available = 0;
            rc = HW_WINDOW;
        }
    }
    if (!available) {
        int first = install(PPC_HWB_WINDOW_VA, 0);
        if (!install(PPC_HWB_WINDOW_VA + PAGE_SIZE, 0) || !first)
            arch_cpu_stop();
    }
    busy = 0;
end:
    ppc_irq_restore(irq);
    if (!store && rc == HW_OK) *value = result;
    return rc;
}
