/* PEM 7.7.1/7.7.3; OpenBSD powerpc/powerpc/pmap.c:842-854,
 * 2451-2552 (ordering and old-EA reconstruction), macppc/locore.S:853-949
 * (miss gate). Single CPU/root, fixed VSIDs; no allocation or A/D writeback.
 * This kernel provider is separate from the standalone pmap.c image. */
#include "ppc_hpt.h"
#include "ppc_frame.h"
#include "ppc_pd.h"

enum {
    HPT_BYTES = 128 * 1024, HPT_HASH_MASK = 0x7ff,
    HPT_HTABMASK = 1, HPT_VSID_BASE = 0x10,
    HPT_API_MASK = 0x3f, HPT_VSID_SHIFT = 7,
    HPT_MSR_PR = 0x4000, HPT_ISI_CAUSES = 0x78000000,
    HPT_GUARDED = 0x08
};
static volatile u32 *hpt;
static u32 hpt_pa, victim_next;

/* The host replaces only privileged operations and observes native word stores. */
#ifdef PPC_HPT_HOST_TEST
void ppc_hpt_write(volatile u32 *word, u32 value);
void ppc_hpt_tlbie(u32 va);
#else
static inline void ppc_hpt_write(volatile u32 *word, u32 value)
{
    *word = value;
}
static inline void ppc_hpt_tlbie(u32 va)
{
    __asm__ volatile("tlbie %0" : : "r"(va) : "memory");
}
#endif

u32 ppc_hpt_vsid(u32 segment)
{
    return HPT_VSID_BASE | segment;
}

u32 ppc_hpt_sdr1(void)
{
    return hpt ? hpt_pa | HPT_HTABMASK : 0;
}

void ppc_hpt_clear(void)
{
    if (!hpt)
        return;
    for (u32 i = 0; i < HPT_BYTES / sizeof(u32); ++i)
        hpt[i] = 0;
    ppc_sync();
    victim_next = 0;
}

int ppc_hpt_init(u32 pa, u32 size)
{
    if (!pa || size != HPT_BYTES || (pa & (HPT_BYTES - 1)))
        return 0;
    hpt_pa = pa;
    hpt = ppc_hpt_ptr(pa); /* Physical pointer, translation remains disabled. */
    ppc_hpt_clear();
    return 1;
}

static u32 hpt_old_va(u32 group, u32 hi)
{
    u32 vsid = (hi >> HPT_VSID_SHIFT) & 0xffffff;
    u32 primary = group ^ ((hi & PPC_PTE_H) ? HPT_HASH_MASK : 0);
    u32 page = (vsid ^ primary) & HPT_HASH_MASK;
    /* API covers page-index bits 10..15; hash supplies 0..10 (overlap agrees).
     * The low VSID nibble uniquely identifies the fixed segment. */
    return ((vsid & 15) << 28) | ((hi & HPT_API_MASK) << 22) |
           (page << PPC_PAGE_SHIFT);
}

static void hpt_delete(u32 slot)
{
    u32 hi = hpt[slot];
    u32 old_va = hpt_old_va(slot / (PPC_PTEG_SIZE / sizeof(u32)), hi);
    ppc_hpt_write(&hpt[slot], hi & ~PPC_PTE_V);
    ppc_sync();
    ppc_hpt_tlbie(old_va);
    ppc_eieio();
    ppc_tlbsync();
    ppc_sync();
}

static int hpt_insert(u32 va, const struct ppc_translation *t)
{
    u32 vsid = ppc_hpt_vsid(va >> 28);
    u32 key = (vsid << HPT_VSID_SHIFT) | ((va >> 22) & HPT_API_MASK);
    u32 hash = (vsid ^ (va >> PPC_PAGE_SHIFT)) & HPT_HASH_MASK;
    u32 free_slot = 0, free_hi = 0;
    int found_free = 0;
    /* Search both groups even after a hole: never create a duplicate VSID/VA.
     * An existing entry on a miss cannot be repaired by another identical fill;
     * return to ordinary fault handling instead of retrying forever. */
    for (u32 secondary = 0; secondary < 2; ++secondary) {
        u32 group = hash ^ (secondary ? HPT_HASH_MASK : 0);
        u32 hi = key | (secondary ? PPC_PTE_H : 0);
        for (u32 i = 0; i < PPC_PTEG_ENTRIES; ++i) {
            u32 slot = group * (PPC_PTEG_SIZE / sizeof(u32)) + i * 2;
            u32 old = hpt[slot];
            if (old == (hi | PPC_PTE_V))
                return 0;
            if (!(old & PPC_PTE_V) && !found_free) {
                free_slot = slot;
                free_hi = hi;
                found_free = 1;
            }
        }
    }
    if (!found_free) {
        /* Round robin across both groups; bounded and deterministic. */
        u32 choice = victim_next++ & 15;
        u32 secondary = choice / PPC_PTEG_ENTRIES;
        u32 group = hash ^ (secondary ? HPT_HASH_MASK : 0);
        free_slot = group * (PPC_PTEG_SIZE / sizeof(u32)) +
                    (choice % PPC_PTEG_ENTRIES) * 2;
        free_hi = key | (secondary ? PPC_PTE_H : 0);
        hpt_delete(free_slot);
    }
    ppc_hpt_write(&hpt[free_slot + 1], (t->pa & PPC_PAGE_MASK) | t->attr);
    ppc_eieio();
    ppc_hpt_write(&hpt[free_slot], free_hi | PPC_PTE_V);
    ppc_sync();
    return 1;
}

int ppc_hpt_spill(struct ppc_frame *f)
{
    if (!f || !hpt)
        return 0;
    u32 va;
    int store = 0;
    if (f->vector == PPC_VECTOR_DSI) {
        /* Reject every other cause, including miss + EAR/DABR/protection. */
        if ((f->dsisr & ~PPC_DSISR_STORE) != PPC_DSISR_MISS)
            return 0;
        va = f->dar;
        store = !!(f->dsisr & PPC_DSISR_STORE);
    } else if (f->vector == PPC_VECTOR_ISI) {
        if ((f->srr1 & HPT_ISI_CAUSES) != PPC_ISI_MISS)
            return 0;
        va = f->srr0;
    } else {
        return 0;
    }
    struct ppc_translation t;
    if (!ppc_pd_lookup(ppc_root_current(), va, store,
                       !!(f->srr1 & HPT_MSR_PR), &t))
        return 0;
    if (f->vector == PPC_VECTOR_ISI && (t.attr & HPT_GUARDED))
        return 0;
    return hpt_insert(va, &t);
}
