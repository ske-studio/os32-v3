/* Acceptance-only x86 CPU/cache observations; Intel SDM Vol.3A 11.11/11.12. */
#include "video_wc.h"
#include "mm/paging.h"
#define ACCEPT_LINE 4096U
#define ACCEPT_MTRR_CAP 0xfeU
#define ACCEPT_MTRR_DEF 0x2ffU
#define ACCEPT_MTRR_BASE 0x200U
#define ACCEPT_MTRR_VALID (1ULL << 11)
#define ACCEPT_MTRR_WC 1U
#define ACCEPT_PAT 0x277U
#define ACCEPT_ID_BIT (1U << 21)
#define ACCEPT_PTE_PAT 0x80U
#define ACCEPT_EXT_MAX 0x80000000U
#define ACCEPT_EXT_WIDTH 0x80000008U
struct cache_log { char line[ACCEPT_LINE], *out; void (*report)(const char *); };
static const char *cache_mode(void) { return GF_ACCEPT_MODE == 0 ? "byte-uc" : GF_ACCEPT_MODE == 1 ? "dword-uc" : "dword-wc"; }
static void cache_word(struct cache_log *log, const char *s) { while (*s) *log->out++ = *s++; *log->out++ = ' '; }
static void cache_number(struct cache_log *log, u32 n)
{
    char digits[10]; u32 count = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) *log->out++ = digits[--count];
    *log->out++ = ' ';
}
static void cache_begin(struct cache_log *log, const char *kind)
{
    log->out = log->line; cache_word(log, "GFA1"); cache_word(log, GF_ACCEPT_ID); cache_word(log, cache_mode()); cache_word(log, kind);
}
static void cache_emit(struct cache_log *log)
{
    *log->out++ = '\n'; *log->out = 0;
    log->report(log->line);
}
/* Read-only CPU primitives begin. Replaced by instruction-free host models. */
static u32 flags(void)
{
    u32 v; __asm__ volatile("pushfl; popl %0" : "=r"(v) : : "memory"); return v;
}
static void set_flags(u32 v)
{
    __asm__ volatile("pushl %0; popfl" : : "r"(v) : "cc", "memory");
}
static void cpuid(u32 leaf, u32 sub, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(sub) : "memory");
}
static unsigned long long msr(u32 reg)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg) : "memory");
    return ((unsigned long long)hi << 32) | lo;
}
/* Read-only CPU primitives end. */
static void cpu_record(struct cache_log *log, u32 leaf, u32 sub, u32 a, u32 b, u32 c, u32 d)
{
    cache_begin(log, "cpu"); cache_number(log, leaf); cache_number(log, sub); cache_number(log, a); cache_number(log, b); cache_number(log, c); cache_number(log, d); cache_emit(log);
}
static unsigned long long msr_record(struct cache_log *log, u32 phase, u32 reg)
{
    unsigned long long value = msr(reg);
    cache_begin(log, "msr"); cache_number(log, phase); cache_number(log, reg); cache_number(log, (u32)value); cache_number(log, value >> 32); cache_emit(log);
    return value;
}
/* Return 1 for confirmed UC, 2 for WC, 0 for an unclassified mapping.
 * This observes BIOS state after wc_stop and actual state after wc_start.
 * Partial/ambiguous WC never becomes a UC measurement. */
int arch_video_cache_inspect(u32 phase, u32 base, u32 bytes, void (*report)(const char *))
{
    struct cache_log storage, *log = &storage;
    log->report = report;
    int mapped = 1, result = 1;
    for (u32 i = 0; i < (bytes / PAGE_SIZE); ++i) {
        u32 address = base + i * PAGE_SIZE, raw = paging_pte_raw(address);
        cache_begin(log, "pte"); cache_number(log, phase); cache_number(log, i); cache_number(log, raw); cache_emit(log);
        if ((raw & (PTE_FRAME_MASK | PTE_PRESENT | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT | ACCEPT_PTE_PAT)) !=
            (address | PTE_PRESENT | PAGE_RW | PTE_PCD)) mapped = 0;
    }
    u32 saved = flags(); set_flags(saved ^ ACCEPT_ID_BIT); u32 changed = flags(); set_flags(saved);
    if (!((saved ^ changed) & ACCEPT_ID_BIT)) return mapped ? 1 : 0;
    u32 a,b,c,d,maximum;
    cpuid(0,0,&a,&b,&c,&d); maximum = a; cpu_record(log, 0,0,a,b,c,d);
    if (!maximum) return mapped ? 1 : 0;
    int intel = b == 0x756e6547U && d == 0x49656e69U && c == 0x6c65746eU;
    cpuid(1,0,&a,&b,&c,&d); cpu_record(log, 1,0,a,b,c,d);
    u32 features = d;
    if (phase == 0 && maximum >= 2) {
        cpuid(2,0,&a,&b,&c,&d); u32 count = a & 0xffU;
        cpu_record(log, 2,0,a,b,c,d);
        for (u32 i=1; i<count; ++i) { cpuid(2,0,&a,&b,&c,&d); cpu_record(log, 2,i,a,b,c,d); }
        if (maximum >= 4) for (u32 i=0; ; ++i) {
            cpuid(4,i,&a,&b,&c,&d); cpu_record(log, 4,i,a,b,c,d);
            if (!(a & 0x1fU)) break;
        }
    }
    if (!intel) return 0; /* The WC implementation also requires GenuineIntel. */
    if (!(features & (1U << 5))) return mapped ? 1 : 0;
    if ((features & (1U << 16)) && ((msr_record(log, phase, ACCEPT_PAT) >> 16) & 0xffU) != 7) mapped = 0;
    if (!(features & (1U << 12))) return mapped ? 1 : 0;
    unsigned long long cap = msr_record(log, phase,ACCEPT_MTRR_CAP), def = msr_record(log, phase,ACCEPT_MTRR_DEF);
    u32 bits = 36;
    cpuid(ACCEPT_EXT_MAX,0,&a,&b,&c,&d); cpu_record(log, ACCEPT_EXT_MAX,0,a,b,c,d);
    if (a >= ACCEPT_EXT_WIDTH) { cpuid(ACCEPT_EXT_WIDTH,0,&a,&b,&c,&d); bits=a & 0xffU; cpu_record(log, ACCEPT_EXT_WIDTH,0,a,b,c,d); }
    if (bits < 32 || bits > 52) return 0;
    unsigned long long mask = ((1ULL << bits)-1) & ~(unsigned long long)(PAGE_SIZE-1);
    if ((def & ACCEPT_MTRR_VALID) && (def & 0xffU) == ACCEPT_MTRR_WC) result = 2;
    for (u32 i=0; i<(cap & 0xffU); ++i) {
        unsigned long long range_base = msr_record(log, phase,ACCEPT_MTRR_BASE+2*i);
        unsigned long long size = msr_record(log, phase,ACCEPT_MTRR_BASE+2*i+1);
        if (!(def & ACCEPT_MTRR_VALID) || !(size & ACCEPT_MTRR_VALID) || (range_base & 0xffU) != ACCEPT_MTRR_WC) continue;
        unsigned long long first = range_base & size & mask, end = first + ((~size & mask) | (PAGE_SIZE-1)) + 1;
        if (base < end && first < (unsigned long long)base+bytes) {
            if (first <= base && end >= (unsigned long long)base+bytes) result = 2;
            else mapped = 0;
        }
    }
    return mapped ? result : 0;
}
