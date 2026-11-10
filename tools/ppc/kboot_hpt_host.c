/* 32-bit, libc-free observer. Link actual HPT and C1 walker; fake only
 * privileged operations and observe stores (no replacement insertion/walk). */
#include "arch/ppc/ppc_hpt.h"
#include "arch/ppc/ppc_frame.h"
#include "mm/paging.h"
static void exit_code(int code) __attribute__((noreturn));
static void exit_code(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
static void failure(unsigned line)
{
    char text[16];
    unsigned n = 0;
    do { text[n++] = '0' + line % 10; line /= 10; } while (line);
    for (unsigned i = 0; i < n / 2; ++i) {
        char c = text[i]; text[i] = text[n - 1 - i]; text[n - 1 - i] = c;
    }
    text[n++] = '\n';
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(n) : "memory");
    exit_code(1);
}
#define CHECK(x) do { if (!(x)) failure(__LINE__); } while (0)
static u32 memory[32770]; /* guards on both ends of 128KiB */
static u32 pd[1024] __attribute__((aligned(4096)));
static u32 pt[1024] __attribute__((aligned(4096)));
static u32 root;
struct event { char kind; u32 slot, value; };
static struct event events[32];
static unsigned count, root_reads;
static void record(char kind, u32 slot, u32 value)
{
    CHECK(count < 32);
    events[count++] = (struct event){kind, slot, value};
}
volatile u32 *ppc_hpt_ptr(u32 pa)
{
    CHECK(pa == 0x200000 || pa == 0xfffe0000U);
    return &memory[1];
}
u32 ppc_root_current(void) { ++root_reads; return root; }
void ppc_hpt_write(volatile u32 *word, u32 value)
{
    CHECK(word >= &memory[1] && word < &memory[32769]);
    record('W', word - &memory[1], value);
    *word = value;
}
void ppc_sync(void) { record('S', 0, 0); }
void ppc_eieio(void) { record('E', 0, 0); }
void ppc_tlbsync(void) { record('B', 0, 0); }
void ppc_hpt_tlbie(u32 va) { record('T', 0, va); }
static void reset(void)
{
    count = root_reads = 0;
    memory[0] = 0xabcddcba; memory[32769] = 0x12344321;
    ppc_hpt_clear();
    CHECK(count == 1 && events[0].kind == 'S');
    for (unsigned i = 1; i <= 32768; ++i) CHECK(memory[i] == 0);
    CHECK(memory[0] == 0xabcddcba && memory[32769] == 0x12344321);
    count = 0;
}
static struct ppc_frame frame(u32 va, int isi)
{
    struct ppc_frame f = {0};
    f.vector = isi ? 0x400 : 0x300;
    f.srr0 = va;
    f.dar = va;
    f.srr1 = isi ? 0x40000000 : 0;
    f.dsisr = 0x40000000;
    return f;
}
static void map(u32 va, u32 directory_flags, u32 leaf_flags)
{
    root = (u32)pd;
    pd[va >> 22] = (u32)pt | directory_flags;
    pt[(va >> 12) & 1023] = 0x76543000 | leaf_flags;
}
static void insertion(unsigned slot, u32 hi, int evict, u32 old_hi, u32 old_va, u32 new_va)
{
    unsigned j = 0;
    const char *order = evict ? "WSTEBSWEWS" : "WEWS";
    CHECK(count == (evict ? 10U : 4U));
    for (unsigned i = 0; i < count; ++i) CHECK(events[i].kind == order[i]);
    if (evict) {
        CHECK(events[0].slot == slot && events[0].value == (old_hi & 0x7fffffff));
        CHECK(events[2].value == old_va && events[2].value != (new_va & 0xfffff000));
        j = 6;
    }
    CHECK(events[j].slot == slot + 1 && events[j].value == 0x76543012);
    CHECK(events[j+2].slot == slot && events[j+2].value == hi);
    CHECK(memory[slot+1] == hi && memory[slot+2] == 0x76543012);
}
static void collisions(void)
{
    /* Each segment, both hash bit-10 values, hash edge, both victim groups,
     * all API values, and every slot. Known original VAs are the oracle. */
    const unsigned lows[] = {0, 0x400, 0x7ff};
    for (unsigned seg = 0; seg < 16; ++seg)
    for (unsigned l = 0; l < 3; ++l)
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        reset();
        u32 old_va[16] = {0}, old_hi[16] = {0};
        unsigned hash = (0x10 + seg) ^ lows[l];
        for (unsigned n = 0; n < 32; ++n) {
            unsigned page = ((reverse ? 31-n : n) * 2048) + lows[l];
            u32 va = (seg << 28) | (page << 12) | 0xabc;
            unsigned choice = n < 16 ? n : n-16;
            unsigned secondary = choice >= 8;
            unsigned group = secondary ? 2047-hash : hash;
            unsigned slot = group * 16 + (choice % 8) * 2;
            u32 hi = 0x80000000U | ((0x10 + seg) << 7) |
                     ((page / 1024) & 63) | (secondary ? 64 : 0);
            map(va, 7, 7);
            struct ppc_frame f = frame(va, n & 1);
            f.srr1 |= 0x4000; /* USER, including the ISI path */
            if (!(n & 1)) f.dsisr |= 0x02000000; /* store */
            count = root_reads = 0;
            CHECK(ppc_hpt_spill(&f) == 1 && root_reads == 1);
            insertion(slot, hi, n >= 16, old_hi[choice], old_va[choice], va);
            old_va[choice] = va & 0xfffff000;
            old_hi[choice] = hi;
            count = 0;
            CHECK(ppc_hpt_spill(&f) == 0 && count == 0); /* no duplicate, even full */
        }
        /* Duplicate in secondary must be detected despite a hole in primary. */
        memory[hash * 16 + 1] = 0;
        u32 va = old_va[15];
        map(va, 7, 7);
        struct ppc_frame f = frame(va, 0);
        count = 0;
        CHECK(ppc_hpt_spill(&f) == 0 && count == 0);
    }
}
static void faults(void)
{
    const u32 va = 0xa1234567;
    const u32 dsi[] = {0, 0x08000000, 0x48000000, 0xc0000000,
                      0x44000000, 0x40400000, 0x40100000};
    const u32 isi[] = {0, 0x08000000, 0x10000000, 0x48000000, 0x50000000};
    reset();
    map(va, 7, 7);
    for (unsigned i = 0; i < sizeof(dsi)/sizeof(dsi[0]); ++i) {
        struct ppc_frame f = frame(va, 0); f.dsisr = dsi[i];
        count = root_reads = 0;
        CHECK(ppc_hpt_spill(&f) == 0 && count == 0 && root_reads == 0);
    }
    for (unsigned i = 0; i < sizeof(isi)/sizeof(isi[0]); ++i) {
        struct ppc_frame f = frame(va, 1); f.srr1 = isi[i];
        count = root_reads = 0;
        CHECK(ppc_hpt_spill(&f) == 0 && count == 0 && root_reads == 0);
    }
    struct ppc_frame f = frame(va, 0);
    f.vector = 0x900;
    CHECK(ppc_hpt_spill(&f) == 0 && ppc_hpt_spill(0) == 0);
    /* Actual C1 permission and guard checks, for both DSI and ISI. */
    const unsigned flags[][2] = {{0,7}, {7,0}, {3,7}, {7,3}, {0x87,7}, {7,0x87}};
    for (unsigned i = 0; i < 6; ++i)
    for (unsigned instruction = 0; instruction < 2; ++instruction) {
        map(va, flags[i][0], flags[i][1]);
        f = frame(va, instruction); f.srr1 |= 0x4000;
        count = 0;
        CHECK(ppc_hpt_spill(&f) == 0 && count == 0);
    }
    map(va, 7, 5); f = frame(va, 0); f.srr1 = 0x4000; f.dsisr |= 0x02000000;
    CHECK(ppc_hpt_spill(&f) == 0);
    map(va, 7, 0x17); f = frame(va, 1);
    CHECK(ppc_hpt_spill(&f) == 0); /* no instruction fill of I/G memory */
    for (unsigned i = 1; i <= 32768; ++i) CHECK(memory[i] == 0);
    /* Different DAR/SRR0 prove vector selects the correct VA. */
    map(va, 7, 7); f = frame(va, 1); f.dar = 0xb0000000;
    count = 0; CHECK(ppc_hpt_spill(&f) == 1);
    reset();
    f = frame(va, 0); f.srr0 = 0xb0000000;
    count = 0; CHECK(ppc_hpt_spill(&f) == 1);
}
int main(void)
{
    struct ppc_frame f = frame(0, 0);
    CHECK(ppc_hpt_sdr1() == 0 && ppc_hpt_spill(&f) == 0);
    ppc_hpt_clear();
    CHECK(!ppc_hpt_init(0, 131072));
    CHECK(!ppc_hpt_init(0x200000, 65536));
    CHECK(!ppc_hpt_init(0x210000, 131072));
    CHECK(ppc_hpt_sdr1() == 0);
    CHECK(ppc_hpt_init(0xfffe0000U, 131072));
    CHECK(ppc_hpt_sdr1() == 0xfffe0001U);
    count = 0;
    CHECK(ppc_hpt_init(0x200000, 131072));
    CHECK(ppc_hpt_sdr1() == 0x200001);
    CHECK(!ppc_hpt_init(0x200004, 131072) && ppc_hpt_sdr1() == 0x200001);
    for (unsigned i = 0; i < 16; ++i) CHECK(ppc_hpt_vsid(i) == 0x10+i);
    faults();
    collisions();
    for (unsigned i = 1; i <= 32768; ++i) memory[i] = 0xffffffff;
    reset();
    return 0;
}
void _start(void) { exit_code(main()); }
