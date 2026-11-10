/* PEM 7.7.3, table 7-20 (Ks=0/Kp=1); 5.3.1 (WIMG).
 * OpenBSD powerpc/powerpc/pmap.c:2287-2333: reject absent/protected
 * translations before insertion. Our software PD/PT format is OS32-specific.
 */
#include "ppc_pd.h"
#include "mm/paging.h"

/* HPT low-word encodings, distinct from the software table bit namespace. */
enum {
    PD_PP_SUPERVISOR = 0, PD_PP_USER_RO = 1, PD_PP_USER_RW = 2,
    PD_W = 0x40, PD_I = 0x20, PD_M = 0x10, PD_G = 0x08
};

static u32 pd_read_real(u32 pa, void *context)
{
    (void)context;
    /* DR=0: P2V would introduce a translated virtual address here. */
    return *(const volatile u32 *)(unsigned long)pa;
}

int ppc_pd_lookup_read(u32 root, u32 va, int store, int user,
                      struct ppc_translation *out,
                      ppc_pd_read_fn read_word, void *context)
{
    if (!out || !read_word || (root & ~PTE_FRAME_MASK))
        return 0;
    u32 pde = read_word(root + PDE_INDEX(va) * sizeof(u32), context);
    if (!(pde & PTE_PRESENT))
        return 0;
    if (pde & PTE_PS)
        return 0;
    u32 pte = read_word((pde & PTE_FRAME_MASK) +
                        PTE_INDEX(va) * sizeof(u32), context);
    if (!(pte & PTE_PRESENT))
        return 0;
    /* Bit 7 is PS in a PDE, PAT in a 4KiB leaf. PAT is unsupported. */
    if (pte & PTE_PS)
        return 0;
    u32 effective = pde & pte;
    if (user && (!(effective & PTE_USER) ||
                 (store && !(effective & PTE_RW))))
        return 0;

    u32 pp = PD_PP_SUPERVISOR;
    if (effective & PTE_USER)
        pp = (effective & PTE_RW) ? PD_PP_USER_RW : PD_PP_USER_RO;
    u32 cache = PD_M;
    if (pte & PTE_PCD)
        cache = PD_I | PD_G;
    else if (pte & PTE_PWT)
        cache = PD_W;
    out->pa = (pte & PTE_FRAME_MASK) | (va & ~PTE_FRAME_MASK);
    out->attr = cache | pp;
    /* Deliberately no A/D writeback and no invented per-page NX. */
    return 1;
}

int ppc_pd_lookup(u32 root, u32 va, int store, int user,
                  struct ppc_translation *out)
{
    return ppc_pd_lookup_read(root, va, store, user, out, pd_read_real, 0);
}
