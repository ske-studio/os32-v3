/* docs/design/drivers/video/geforce/gf_accept.md — acceptance builds only.
 * Read-only CPU observations follow Intel SDM Vol.3A 11.11/11.12;
 * all MTRR changes remain in the kernel-owned wc_start/wc_stop callbacks. */
#include "geforce_module.h"
#include "hwdev/gf/production.h"
#include "sys.h"
#include "mm/paging.h"
#include "mm/pgalloc.h"
#include "drivers/pci/pci.h"

#define ACCEPT_WARMUP 10U
#define ACCEPT_COUNT 100U
#define ACCEPT_RUNS 3U
#define ACCEPT_FAILED (~0U)
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
#define ACCEPT_PAGE_COUNT (GF_MODE_VRAM_BYTES / PAGE_SIZE)
static const struct { int x, y, w, h; } rectangles[] = {
    {0,0,0,0}, {0,0,GF_MODE_WIDTH,GF_MODE_HEIGHT}, {0,0,320,200},
    {0,0,GF_MODE_WIDTH,16}, {0,0,32,32}, {0,0,16,16}, {1,0,63,32},
    {0,0,1,GF_MODE_HEIGHT}, {GF_MODE_WIDTH-1,GF_MODE_HEIGHT-1,1,1}
};
static u8 *pattern;
static u32 samples[ACCEPT_COUNT * ACCEPT_RUNS];
static char line[ACCEPT_LINE], *out;
static const char *effective = "unknown", *reason = "unobserved";
static int initialized, measured, usable, clock_ok;
static u32 points;
static int (*real_point)(void *);
static void *real_opaque;
extern int gf_present_rect(const u8 *, u32, volatile u8 *, u32, volatile u8 *,
                          int, int, int, int, int (*)(void *), void *);
static const char *mode(void)
{
    return GF_ACCEPT_MODE == 0 ? "byte-uc" : GF_ACCEPT_MODE == 1 ? "dword-uc" : "dword-wc";
}
static void word(const char *s) { while (*s) *out++ = *s++; *out++ = ' '; }
static void number(u32 n)
{
    char digits[10]; u32 count = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) *out++ = digits[--count];
    *out++ = ' ';
}
static void begin(const char *kind)
{
    out = line; word("GFA1"); word(GF_ACCEPT_ID); word(mode()); word(kind);
}
static void emit(void)
{
    *out++ = '\n'; *out = 0;
    geforce_context->platform->report(line);
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
static void cpu_record(u32 leaf, u32 sub, u32 a, u32 b, u32 c, u32 d)
{
    begin("cpu"); number(leaf); number(sub); number(a); number(b); number(c); number(d); emit();
}
static unsigned long long msr_record(u32 phase, u32 reg)
{
    unsigned long long value = msr(reg);
    begin("msr"); number(phase); number(reg); number((u32)value); number(value >> 32); emit();
    return value;
}
/* Return 1 for confirmed UC, 2 for WC, 0 for an unclassified mapping.
 * This observes BIOS state after wc_stop and actual state after wc_start.
 * Partial/ambiguous WC never becomes a UC measurement. */
static int inspect(u32 phase)
{
    const struct geforce_platform *p = geforce_context->platform;
    int mapped = 1, result = 1;
    for (u32 i = 0; i < ACCEPT_PAGE_COUNT; ++i) {
        u32 address = p->display_phys + i * PAGE_SIZE, raw = paging_pte_raw(address);
        begin("pte"); number(phase); number(i); number(raw); emit();
        if ((raw & (PTE_FRAME_MASK | PTE_PRESENT | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT | ACCEPT_PTE_PAT)) !=
            (address | PTE_PRESENT | PAGE_RW | PTE_PCD)) mapped = 0;
    }
    u32 saved = flags(); set_flags(saved ^ ACCEPT_ID_BIT); u32 changed = flags(); set_flags(saved);
    if (!((saved ^ changed) & ACCEPT_ID_BIT)) return mapped ? 1 : 0;
    u32 a,b,c,d,maximum;
    cpuid(0,0,&a,&b,&c,&d); maximum = a; cpu_record(0,0,a,b,c,d);
    if (!maximum) return mapped ? 1 : 0;
    int intel = b == 0x756e6547U && d == 0x49656e69U && c == 0x6c65746eU;
    cpuid(1,0,&a,&b,&c,&d); cpu_record(1,0,a,b,c,d);
    u32 features = d;
    if (phase == 0 && maximum >= 2) {
        cpuid(2,0,&a,&b,&c,&d); u32 count = a & 0xffU;
        cpu_record(2,0,a,b,c,d);
        for (u32 i=1; i<count; ++i) { cpuid(2,0,&a,&b,&c,&d); cpu_record(2,i,a,b,c,d); }
        if (maximum >= 4) for (u32 i=0; ; ++i) {
            cpuid(4,i,&a,&b,&c,&d); cpu_record(4,i,a,b,c,d);
            if (!(a & 0x1fU)) break;
        }
    }
    if (!intel) return 0; /* The WC implementation also requires GenuineIntel. */
    if (!(features & (1U << 5))) return mapped ? 1 : 0;
    if ((features & (1U << 16)) && ((msr_record(phase, ACCEPT_PAT) >> 16) & 0xffU) != 7) mapped = 0;
    if (!(features & (1U << 12))) return mapped ? 1 : 0;
    unsigned long long cap = msr_record(phase,ACCEPT_MTRR_CAP), def = msr_record(phase,ACCEPT_MTRR_DEF);
    u32 bits = 36;
    cpuid(ACCEPT_EXT_MAX,0,&a,&b,&c,&d); cpu_record(ACCEPT_EXT_MAX,0,a,b,c,d);
    if (a >= ACCEPT_EXT_WIDTH) { cpuid(ACCEPT_EXT_WIDTH,0,&a,&b,&c,&d); bits=a & 0xffU; cpu_record(ACCEPT_EXT_WIDTH,0,a,b,c,d); }
    if (bits < 32 || bits > 52) return 0;
    unsigned long long mask = ((1ULL << bits)-1) & ~(unsigned long long)(PAGE_SIZE-1);
    if ((def & ACCEPT_MTRR_VALID) && (def & 0xffU) == ACCEPT_MTRR_WC) result = 2;
    for (u32 i=0; i<(cap & 0xffU); ++i) {
        unsigned long long base = msr_record(phase,ACCEPT_MTRR_BASE+2*i);
        unsigned long long size = msr_record(phase,ACCEPT_MTRR_BASE+2*i+1);
        if (!(def & ACCEPT_MTRR_VALID) || !(size & ACCEPT_MTRR_VALID) || (base & 0xffU) != ACCEPT_MTRR_WC) continue;
        unsigned long long first = base & size & mask, end = first + ((~size & mask) | (PAGE_SIZE-1)) + 1;
        if (p->display_phys < end && first < (unsigned long long)p->display_phys+GF_MODE_VRAM_BYTES) {
            if (first <= p->display_phys && end >= (unsigned long long)p->display_phys+GF_MODE_VRAM_BYTES) result = 2;
            else mapped = 0;
        }
    }
    return mapped ? result : 0;
}
static int counted_point(void *opaque)
{
    (void)opaque; ++points; return real_point(real_opaque);
}
static unsigned long long now(void)
{
    u32 lo=0,hi=0;
    if (sys_time_now(&lo,&hi)) clock_ok = 0;
    return ((unsigned long long)hi << 32) | lo;
}
static int initialize(void)
{
    const struct geforce_platform *p = geforce_context->platform;
    if (!p->wc_stop || !p->wc_start || !p->report) return -1;
    if (p->wc_stop() < 0) return -1;
    int original = inspect(0), actual = original;
    if (GF_ACCEPT_MODE == 2) {
        int rc = p->wc_start();
        if (rc < 0) return -1;
        actual = inspect(1);
        usable = rc == 1 && actual == 2;
        reason = usable ? "ok" : "wc-unavailable";
    } else {
        usable = original == 1;
        reason = usable ? "ok" : original == 2 ? "bios-wc" : "unclassified";
        (void)inspect(1);
    }
    effective = actual == 1 ? "UC" : actual == 2 ? "WC" : "unknown";
    begin("pair"); number(GF_ACCEPT_PAIR); emit();
    begin("state"); word(effective); word(usable ? "OK" : "SKIP"); word(reason); emit();
    u32 config = pci_cfg_read32(RECOVER_GF_BUS,RECOVER_GF_DEV,0,PCI_CFG_CACHE_LINE);
    begin("pci"); number(pci_cfg_read16(RECOVER_GF_BUS,RECOVER_GF_DEV,0,PCI_CFG_COMMAND)); number(config);
    for (u32 i=0; i<RECOVER_BAR_COUNT; ++i) number(p->resources->bars[i].raw);
    emit();
    return 0;
}
static int measure(volatile u8 *display, u32 pitch, volatile u8 *bar0)
{
    if (!usable) return 0;
    /* The camera sees this fixed 0..255 RAM pattern while records are sent.
     * It is drawn once outside timing, after the GF route is already active. */
    if (gf_present_rect(pattern,GF_MODE_PITCH,display,pitch,bar0,0,0,GF_MODE_WIDTH,GF_MODE_HEIGHT,counted_point,0)) return -1;
    begin("pattern"); word("0..255"); emit();
    for (u32 c=0; c<sizeof(rectangles)/sizeof(rectangles[0]); ++c) {
        int fail=0; u32 warm;
        for (warm=0; warm<ACCEPT_WARMUP; ++warm) {
            if (c && gf_present_rect(pattern,GF_MODE_PITCH,display,pitch,bar0,rectangles[c].x,rectangles[c].y,
                rectangles[c].w,rectangles[c].h,counted_point,0)) return -1;
        }
        u32 run_points[ACCEPT_RUNS] = {0};
        for (u32 i=0; i<ACCEPT_COUNT*ACCEPT_RUNS; ++i) {
            points=0;
            clock_ok=1;
            unsigned long long start=now();
            int rc=c ? gf_present_rect(pattern,GF_MODE_PITCH,display,pitch,bar0,rectangles[c].x,rectangles[c].y,
                rectangles[c].w,rectangles[c].h,counted_point,0) : 0;
            unsigned long long end=now(), elapsed=end-start;
            samples[i]=clock_ok && !rc && end>=start && elapsed<ACCEPT_FAILED ? (u32)elapsed : ACCEPT_FAILED;
            run_points[i/ACCEPT_COUNT] += points;
            if (rc) { fail=1; for (++i; i<ACCEPT_COUNT*ACCEPT_RUNS; ++i) samples[i]=ACCEPT_FAILED; break; }
        }
        u32 bad=0;
        /* Never touch BARs after an identity/point failure. The backend's
         * existing failed-transfer path performs CPU-side drain and stop. */
        if (!fail && c) {
            if (real_point(real_opaque)) fail=1;
            else for (int y=rectangles[c].y; y<rectangles[c].y+rectangles[c].h; ++y)
                for (int x=rectangles[c].x; x<rectangles[c].x+rectangles[c].w; ++x)
                    if (display[(u32)y*pitch+x] != pattern[(u32)y*GF_MODE_PITCH+x]) ++bad;
        }
        for (u32 r=0; r<ACCEPT_RUNS; ++r) {
            begin("sample"); number(c); number(r); number(rectangles[c].w*rectangles[c].h);
            number(run_points[r]); number(fail); number(bad);
            for (u32 i=0; i<ACCEPT_COUNT; ++i) number(samples[r*ACCEPT_COUNT+i]);
            emit();
        }
        if (fail || bad) return -1;
    }
    begin("done"); word("OK"); emit();
    return 0;
}
/* Only backend_geforce.o in the acceptance build names this entry. The
 * transfer implementation itself, optimization and production entry remain
 * unchanged. First call precedes routing; second call can show the pattern. */
int gf_accept_transfer(const u8 *client, u32 client_pitch, volatile u8 *display,
                       u32 display_pitch, volatile u8 *bar0, int x, int y, int w, int h,
                       int (*point)(void *), void *opaque)
{
    real_point=point; real_opaque=opaque;
    /* A later GUI start can acquire WC again through the ordinary backend.
     * Keep UC acceptance transfers UC across reentry as well. */
    if (GF_ACCEPT_MODE != 2 && geforce_context->platform->wc_stop() < 0) return -1;
    if (!initialized) { initialized=1; if (initialize()) return -1; }
    else if (!measured) {
        measured=1;
        if (usable) {
            u32 phys=pgalloc_alloc_phys(LEDGER_OWNER_KERNEL,ACCEPT_PAGE_COUNT);
            if (!phys) return -1;
            pattern=P2V(phys);
            for (u32 i=0; i<GF_MODE_VRAM_BYTES; ++i) pattern[i]=(u8)i;
            int rc=measure(display,display_pitch,bar0);
            int freed=pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,phys/PAGE_SIZE,ACCEPT_PAGE_COUNT);
            pattern=0;
            if (rc || freed) return -1;
        }
        /* Restore all of CLIENT after the synthetic full-screen pattern. */
        x=0; y=0; w=GF_MODE_WIDTH; h=GF_MODE_HEIGHT;
    }
    return gf_present_rect(client,client_pitch,display,display_pitch,bar0,x,y,w,h,point,opaque);
}
