/* Intel SDM Vol.3A 253668-060US, 11.11.1-4/7-8 and 11.12.
 * The caller owns the device range and drawing lifetime; this owns CPU state.
 */
#include "mtrr_wc.h"

#define EFLAGS_ID             (1UL << 21)
#define CPUID_MSR             (1UL << 5)
#define CPUID_MTRR            (1UL << 12)
#define CPUID_PAT             (1UL << 16)
#define CPUID_EXT_MAX         0x80000000UL
#define CPUID_ADDRESS_WIDTH   0x80000008UL
#define INTEL_EBX             0x756e6547UL
#define INTEL_EDX             0x49656e69UL
#define INTEL_ECX             0x6c65746eUL
#define MSR_MTRRCAP           0xfe
#define MSR_PHYSBASE          0x200
#define MSR_PHYSMASK          (MSR_PHYSBASE + 1)
#define MSR_DEF_TYPE          0x2ff
#define MSR_PAT               0x277
#define MTRRCAP_WC            (1ULL << 10)
#define MTRR_ENABLE           (1ULL << 11)
#define MTRR_VALID            (1ULL << 11)
#define MTRR_WC               1
#define BYTE_MASK             0xff
#define PAT2_SHIFT            16
#define PAT_UC_MINUS          7
#define PAGE_BYTES            0x1000ULL
#define FIXED_AREA_END        0x100000UL
#define ADDRESS32_END         (1ULL << 32)
#define FALLBACK_PHYS_BITS    36
#define MIN_PHYS_BITS         32
#define MAX_PHYS_BITS         52
#define CR0_CD                (1UL << 30)
#define CR0_NW                (1UL << 29)
#define CR4_PGE               (1UL << 7)

/* CPU primitives begin. */
static u32 wc_read_flags(void)
{
    u32 value;
    __asm__ volatile("pushfl; popl %0" : "=r"(value) : : "memory");
    return value;
}
static void wc_write_flags(u32 value)
{
    __asm__ volatile("pushl %0; popfl" : : "r"(value) : "cc", "memory");
}
static void wc_cli(void)
{
    /* ARCH-ASM-OK: SDM 11.11.7 cache/MTRR transaction, paired EFLAGS restore. */
    __asm__ volatile("cli" : : : "memory");
}
static void wc_cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0) : "memory");
}
static unsigned long long wc_rdmsr(u32 reg)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg) : "memory");
    return ((unsigned long long)hi << 32) | lo;
}
static void wc_wrmsr(u32 reg, unsigned long long value)
{
    __asm__ volatile("wrmsr" : : "c"(reg), "a"((u32)value), "d"((u32)(value >> 32)) : "memory");
}
static u32 wc_read_cr0(void)
{
    u32 value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value) : : "memory");
    return value;
}
static u32 wc_read_cr4(void)
{
    u32 value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value) : : "memory");
    return value;
}
static void wc_write_cr0(u32 value)
{
    __asm__ volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}
static void wc_write_cr4(u32 value)
{
    __asm__ volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}
static void wc_wbinvd(void)
{
    __asm__ volatile("wbinvd" : : : "memory");
}
static void wc_flush_tlb(void)
{
    u32 value;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=&r"(value) : : "memory");
}
/* CPU primitives end. */

static int wc_cpuid_present(void)
{
    u32 old = wc_read_flags();
    wc_write_flags(old ^ EFLAGS_ID);
    u32 changed = wc_read_flags();
    wc_write_flags(old);
    return !!((old ^ changed) & EFLAGS_ID);
}

struct wc_cpu_state { u32 flags, cr0, cr4; };

static void wc_begin(struct wc_cpu_state *saved)
{
    saved->flags = wc_read_flags();
    saved->cr0 = wc_read_cr0();
    saved->cr4 = wc_read_cr4();
    wc_cli();
    wc_write_cr0((saved->cr0 | CR0_CD) & ~CR0_NW);
    wc_wbinvd();
    if (saved->cr4 & CR4_PGE) wc_write_cr4(saved->cr4 & ~CR4_PGE);
    wc_flush_tlb();
}

static void wc_end(const struct wc_cpu_state *saved)
{
    wc_wbinvd();
    wc_flush_tlb(); /* second flush */
    wc_write_cr0(saved->cr0);
    wc_write_cr4(saved->cr4);
    wc_write_flags(saved->flags);
}

static void wc_write_pair(u32 slot, unsigned long long base, unsigned long long mask)
{
    u32 reg = MSR_PHYSMASK + 2 * slot;
    wc_wrmsr(reg, wc_rdmsr(reg) & ~MTRR_VALID);
    wc_wrmsr(MSR_PHYSBASE + 2 * slot, base);
    wc_wrmsr(reg, mask);
}

static int wc_pair_matches(u32 slot, unsigned long long base, unsigned long long mask)
{
    /* Read both halves even if BASE mismatches. */
    unsigned long long actual_base = wc_rdmsr(MSR_PHYSBASE + 2 * slot);
    unsigned long long actual_mask = wc_rdmsr(MSR_PHYSMASK + 2 * slot);
    return actual_base == base && actual_mask == mask;
}

int x86_wc_acquire(u32 base, u32 bytes, struct x86_wc_lease *lease)
{
    u32 a, b, c, d, bits = FALLBACK_PHYS_BITS;
    u32 required = CPUID_MSR | CPUID_MTRR;
    unsigned long long end = (unsigned long long)base + bytes;
    unsigned long long addr_mask, new_base, new_mask, def_type, cap;
    u32 count, free_slot, covering_slot = 0;
    int overlap = 0, covered = 0;
    struct wc_cpu_state saved;

    lease->active = lease->owned = 0;
    if (base < FIXED_AREA_END || bytes < PAGE_BYTES || (bytes & (bytes - 1)) ||
        (base & (bytes - 1)) || end > ADDRESS32_END) return 0;
    if (!wc_cpuid_present()) return 0;
    wc_cpuid(0, &a, &b, &c, &d);
    if (a < 1 || b != INTEL_EBX || d != INTEL_EDX || c != INTEL_ECX) return 0;
    wc_cpuid(1, &a, &b, &c, &d);
    if ((d & required) != required) return 0;
    cap = wc_rdmsr(MSR_MTRRCAP);
    def_type = wc_rdmsr(MSR_DEF_TYPE);
    if (!(cap & MTRRCAP_WC) || !(def_type & MTRR_ENABLE)) return 0;
    if (d & CPUID_PAT) {
        if (((wc_rdmsr(MSR_PAT) >> PAT2_SHIFT) & BYTE_MASK) != PAT_UC_MINUS) return 0;
    }
    wc_cpuid(CPUID_EXT_MAX, &a, &b, &c, &d);
    if (a >= CPUID_ADDRESS_WIDTH) {
        wc_cpuid(CPUID_ADDRESS_WIDTH, &a, &b, &c, &d);
        bits = a & BYTE_MASK;
    }
    if (bits < MIN_PHYS_BITS || bits > MAX_PHYS_BITS) return 0;
    addr_mask = ((1ULL << bits) - 1) & ~(PAGE_BYTES - 1);
    count = cap & BYTE_MASK;
    free_slot = count;
    for (u32 slot = 0; slot < count; ++slot) {
        unsigned long long range_base = wc_rdmsr(MSR_PHYSBASE + 2 * slot);
        unsigned long long range_mask = wc_rdmsr(MSR_PHYSMASK + 2 * slot);
        if (!(range_mask & MTRR_VALID)) {
            if (free_slot == count) {
                free_slot = slot;
                lease->old_base = range_base;
                lease->old_mask = range_mask;
            }
            continue;
        }
        unsigned long long start = range_base & range_mask & addr_mask;
        unsigned long long limit = start + ((~range_mask & addr_mask) | (PAGE_BYTES - 1)) + 1;
        if ((unsigned long long)base >= limit || end <= start) continue;
        if ((range_base & BYTE_MASK) != MTRR_WC) return 0;
        overlap = 1;
        if (start <= base && limit >= end) {
            covered = 1;
            covering_slot = slot;
        }
    }
    if (covered) {
        lease->slot = covering_slot;
        lease->active = 1;
        return 1;
    }
    if (overlap || free_slot == count) return 0;
    lease->slot = free_slot;
    lease->owned = 1;
    new_base = (base & addr_mask) | MTRR_WC;
    new_mask = (addr_mask & ~((unsigned long long)bytes - 1)) | MTRR_VALID;
    wc_begin(&saved);
    wc_wrmsr(MSR_DEF_TYPE, def_type & ~MTRR_ENABLE);
    wc_write_pair(lease->slot, new_base, new_mask);
    int result = wc_pair_matches(lease->slot, new_base, new_mask) ? 1 : 0;
    if (!result) {
        wc_write_pair(lease->slot, lease->old_base, lease->old_mask);
        result = wc_pair_matches(lease->slot, lease->old_base, lease->old_mask) ? 0 : -1;
    }
    wc_wrmsr(MSR_DEF_TYPE, def_type);
    wc_end(&saved);
    lease->active = 1; /* Fatal rollback retains the recovery lease. */
    if (result >= 0) lease->active = result;
    return result;
}

int x86_wc_release(struct x86_wc_lease *lease)
{
    struct wc_cpu_state saved;
    if (!lease->active) return 0;
    if (!lease->owned) {
        wc_begin(&saved);
        wc_end(&saved);
        lease->active = 0;
        return 0;
    }
    unsigned long long def_type = wc_rdmsr(MSR_DEF_TYPE);
    wc_begin(&saved);
    wc_wrmsr(MSR_DEF_TYPE, def_type & ~MTRR_ENABLE);
    wc_write_pair(lease->slot, lease->old_base, lease->old_mask);
    int restored = wc_pair_matches(lease->slot, lease->old_base, lease->old_mask);
    wc_wrmsr(MSR_DEF_TYPE, def_type);
    wc_end(&saved);
    if (!restored) return -1;
    lease->active = 0;
    return 0;
}
