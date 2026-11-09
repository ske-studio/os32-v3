/* CPU cache attributes and the single video WC lease. */
#include "video_wc.h"
#include "mtrr_wc.h"
#include "mm/paging.h"

#define GF_WC_PTE_PAT PTE_PS /* bit 7 is PAT in a 4 KiB PTE */
#define GF_WC_ID (1U << 21)
#define GF_WC_CPU_REQUIRED ((1U << 5) | (1U << 12))
#define GF_WC_INTEL_B 0x756e6547U
#define GF_WC_INTEL_D 0x49656e69U
#define GF_WC_INTEL_C 0x6c65746eU
#define GF_WC_EXT_MAX 0x80000000U
#define GF_WC_EXT_WIDTH 0x80000008U
#define GF_WC_MTRRCAP 0xfeU
#define GF_WC_DEF_TYPE 0x2ffU
#define GF_WC_PHYSBASE 0x200U
#define GF_WC_PHYSMASK (GF_WC_PHYSBASE + 1)
#define GF_WC_VALID (1ULL << 11)
#define GF_WC_TYPE 1U
#define GF_WC_BYTE_MASK 0xffU
#define GF_WC_FALLBACK_BITS 36U
#define GF_WC_MIN_BITS 32U
#define GF_WC_MAX_BITS 52U
static struct x86_wc_lease lease;
static int wc_result;

/* Read-only CPU primitives begin. No MSR is read before capability checks. */
static u32 gf_wc_flags(void)
{
    u32 v;
    __asm__ volatile("pushfl; popl %0" : "=r"(v) : : "memory");
    return v;
}
static void gf_wc_set_flags(u32 v)
{
    __asm__ volatile("pushl %0; popfl" : : "r"(v) : "cc", "memory");
}
static void gf_wc_cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0) : "memory");
}
static unsigned long long gf_wc_msr(u32 reg)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg) : "memory");
    return ((unsigned long long)hi << 32) | lo;
}
/* Read-only CPU primitives end. */

static int bar0_uc(u32 base, u32 bytes)
{
    u32 a, b, c, d, bits = GF_WC_FALLBACK_BITS, flags = gf_wc_flags();
    gf_wc_set_flags(flags ^ GF_WC_ID);
    u32 changed = gf_wc_flags();
    gf_wc_set_flags(flags);
    if (!((flags ^ changed) & GF_WC_ID)) return 0;
    gf_wc_cpuid(0, &a, &b, &c, &d);
    if (!a || b != GF_WC_INTEL_B || d != GF_WC_INTEL_D || c != GF_WC_INTEL_C) return 0;
    gf_wc_cpuid(1, &a, &b, &c, &d);
    if ((d & GF_WC_CPU_REQUIRED) != GF_WC_CPU_REQUIRED) return 0;
    unsigned long long def = gf_wc_msr(GF_WC_DEF_TYPE);
    if (!(def & GF_WC_VALID) || (def & GF_WC_BYTE_MASK) == GF_WC_TYPE) return 0;
    gf_wc_cpuid(GF_WC_EXT_MAX, &a, &b, &c, &d);
    if (a >= GF_WC_EXT_WIDTH) {
        gf_wc_cpuid(GF_WC_EXT_WIDTH, &a, &b, &c, &d);
        bits = a & GF_WC_BYTE_MASK;
    }
    if (bits < GF_WC_MIN_BITS || bits > GF_WC_MAX_BITS) return 0;
    unsigned long long mask = ((1ULL << bits) - 1) & ~(unsigned long long)(PAGE_SIZE - 1);
    u32 count = gf_wc_msr(GF_WC_MTRRCAP) & GF_WC_BYTE_MASK;
    for (u32 i = 0; i < count; ++i) {
        unsigned long long rb = gf_wc_msr(GF_WC_PHYSBASE + 2 * i);
        unsigned long long rm = gf_wc_msr(GF_WC_PHYSMASK + 2 * i);
        if (!(rm & GF_WC_VALID) || (rb & GF_WC_BYTE_MASK) != GF_WC_TYPE) continue;
        unsigned long long first = rb & rm & mask;
        unsigned long long end = first + ((~rm & mask) | (PAGE_SIZE - 1)) + 1;
        if (base < end && first < (unsigned long long)base + bytes) return 0;
    }
    /* With PCD=1/PWT=0/PAT=0 (PAT2=UC- checked by acquire), non-WC
     * MTRRs yield UC. Conservatively reject any WC overlap/default above. */
    return 1;
}

static int mapped_uc(u32 base, u32 bytes)
{
    const u32 attrs = PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT | GF_WC_PTE_PAT;
    for (u32 off = 0; off < bytes; off += PAGE_SIZE) {
        u32 raw = paging_pte_raw(base + off);
        if ((raw & PTE_FRAME_MASK) != base + off ||
            (raw & attrs) != (PAGE_RW | PTE_PCD)) return 0;
    }
    return 1;
}

static int no_alias(u32 base, u32 mapped, u32 extent)
{
    /* Private app/lease mappings only admit RAM; device PTs are shared and
     * immutable through those APIs. Audit master and the current root. */
    u32 roots[] = {paging_kernel_pd_phys(), paging_current_cr3()};
    for (u32 r = 0; r < sizeof(roots) / sizeof(roots[0]); ++r) {
        if (!roots[r]) return 0;
        if (r && roots[r] == roots[0]) continue;
        const u32 *pd = P2V(roots[r]);
        for (u32 di = 0; di < PDE_COUNT; ++di) {
            u32 d = pd[di];
            if (!(d & PTE_PRESENT)) continue;
            if (d & PTE_PS) return 0; /* OS32 uses 4 KiB device pages only. */
            const u32 *pt = P2V(d & PTE_FRAME_MASK);
            for (u32 ti = 0; ti < PTE_COUNT; ++ti) {
                u32 e = pt[ti], phys = e & PTE_FRAME_MASK;
                if (!(e & PTE_PRESENT) || phys < base || phys - base >= extent) continue;
                u32 va = (di << PDE_SHIFT) | (ti << PAGE_SHIFT);
                if (va != phys || phys - base >= mapped ||
                    (e & (PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT | GF_WC_PTE_PAT)) != (PAGE_RW | PTE_PCD) ||
                    (d & (PAGE_RW | PTE_PWT | PTE_PCD)) != PAGE_RW) return 0;
            }
        }
    }
    return 1;
}

int arch_video_wc_active(void)
{
    return lease.active;
}

int arch_video_wc_eligible(u32 fb, u32 mapped, u32 extent, u32 regs, u32 reg_bytes)
{
    return mapped_uc(fb, mapped) && mapped_uc(regs, reg_bytes) &&
           no_alias(fb, mapped, extent) && bar0_uc(regs, reg_bytes);
}

int arch_video_wc_start(u32 base, u32 bytes)
{
    if (lease.active) return wc_result;
    wc_result = x86_wc_acquire(base, bytes, &lease);
    return wc_result;
}

int arch_video_wc_stop(void)
{
    return x86_wc_release(&lease);
}
