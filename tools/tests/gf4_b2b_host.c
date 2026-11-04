/* PLL helper fixture: independent register oracle from nouveau
 * nvkm/subdev/devinit/nv04.c:184-193, 200-204, 218-226, 247-273;
 * ROM lookup: nvkm/subdev/bios/pll.c:162-175, 301-331. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"

#define ORACLE_GATE 0xc040U
#define ORACLE_STAGE 0x680580U
#define ORACLE_A 0x680508U
#define ORACLE_B 0x680520U
#define ORACLE_COEFF_A 0x680578U
#define ORACLE_COEFF_B 0x68057cU
#define ORACLE_GATE_A (3U << 16)
#define ORACLE_GATE_B (3U << 18)
#define ORACLE_STAGE_A (1U << 8)
#define ORACLE_STAGE_B (1U << 28)
#define ORACLE_S2_ENABLE (1U << 31)
#define ORACLE_S2_KEEP 0x7fff0000U
#define ORACLE_PLL_KEEP 0xfff80000U
#define ORACLE_SINGLE_MAGIC 0x011fU
#define ORACLE_WRITES 5U
#define ROM_HEADER 4U
#define ROM_ENTRY 35U
#define ROM_COUNT 4U
/* Independent ROM field offsets: nvkm/subdev/bios/pll.c:168, 303-327. */
#define ORACLE_ROM_REG 0U
#define ORACLE_ROM_VCO1_MIN_FREQ 4U
#define ORACLE_ROM_VCO1_MAX_FREQ 6U
#define ORACLE_ROM_VCO1_MIN_INPUT 12U
#define ORACLE_ROM_VCO1_MAX_INPUT 16U
#define ORACLE_ROM_VCO1_MIN_N 20U
#define ORACLE_ROM_VCO1_MAX_N 21U
#define ORACLE_ROM_VCO1_MIN_M 22U
#define ORACLE_ROM_VCO1_MAX_M 23U
#define ORACLE_ROM_MAX_P 29U
#define ORACLE_ROM_REFCLK 31U
static u8 rom[ROM_HEADER + ROM_ENTRY * ROM_COUNT];
static void put(u32 at, u32 value, u32 bytes)
{
    for (u32 i = 0; i < bytes; ++i) rom[at + i] = (u8)(value >> (8U * i));
}
static void entry(u32 index, u32 key, u32 max_p)
{
    u32 at = ROM_HEADER + index * ROM_ENTRY;
    put(at + ORACLE_ROM_REG, key, 4);
    put(at + ORACLE_ROM_VCO1_MIN_FREQ, 100, 2); put(at + ORACLE_ROM_VCO1_MAX_FREQ, 500, 2);
    put(at + ORACLE_ROM_VCO1_MIN_INPUT, 27, 2); put(at + ORACLE_ROM_VCO1_MAX_INPUT, 27, 2);
    put(at + ORACLE_ROM_VCO1_MIN_N, 1, 1); put(at + ORACLE_ROM_VCO1_MAX_N, 255, 1);
    put(at + ORACLE_ROM_VCO1_MIN_M, 1, 1); put(at + ORACLE_ROM_VCO1_MAX_M, 1, 1);
    put(at + ORACLE_ROM_MAX_P, max_p, 1); put(at + ORACLE_ROM_REFCLK, 27000, 4);
}
static void sequence_test(u32 head, bool single)
{
    const u32 reg = head ? ORACLE_B : ORACLE_A;
    const u32 coeff = head ? ORACLE_COEFF_B : ORACLE_COEFF_A;
    const u32 gate = head ? ORACLE_GATE_B : ORACLE_GATE_A;
    const u32 stage = head ? ORACLE_STAGE_B : ORACLE_STAGE_A;
    const u32 other = head ? ORACLE_STAGE_A : ORACLE_STAGE_B;
    const u32 saved_gate = ORACLE_GATE_A | ORACLE_GATE_B | 0x55U;
    const u32 saved_stage = other | (single ? 0 : stage) | 0x42U;
    const u32 saved_coeff = 0x52345678U, saved_pll = 0xabc54321U;
    const u32 n1 = 26, m1 = 7, n2 = single ? 1 : 4, m2 = 1, p = 2;
    struct gfinit_pll_limits l = {0};
    struct gfinit_pll_solution s = {n1,m1,n2,m2,p,25071,0};
    struct gfinit_pll_sequence q;
    l.ref = 27000; l.vco1 = (struct gfinit_pll_vco){100000,500000,27000,27000,1,255,1,1};
    assert(gfinit_pll_sequence(reg, &l, &s, &q) == 0);
    assert(q.count == ORACLE_WRITES);
    const u32 regs[] = {ORACLE_GATE, ORACLE_STAGE, coeff, reg, ORACLE_GATE};
    const u32 captures[] = {saved_gate, saved_stage, saved_coeff, saved_pll, saved_gate};
    const u32 expected[] = {saved_gate & ~gate,
        (saved_stage & ~stage) | (single ? stage : 0),
        (saved_coeff & ORACLE_S2_KEEP) | ORACLE_S2_ENABLE | (n2 << 8) | m2 |
            (single ? ORACLE_SINGLE_MAGIC : 0),
        (saved_pll & ORACLE_PLL_KEEP) | (p << 16) | (n1 << 8) | m1,
        saved_gate};
    for (u32 i = 0; i < ORACLE_WRITES; ++i) {
        assert(q.writes[i].reg == regs[i] && q.writes[i].source == regs[i]);
        assert(((captures[i] & q.writes[i].keep) | q.writes[i].value) == expected[i]);
    }
    assert((expected[1] & other) == other);
}
void _start(void)
{
    struct gfinit_pll_limits l;
    rom[0] = 0x21; rom[1] = ROM_HEADER; rom[2] = ROM_ENTRY; rom[3] = ROM_COUNT;
    entry(0, 0, 3); entry(1, ORACLE_A, 4); entry(2, ORACLE_B, 5); entry(3, 0, 6);
    assert(gfinit_pll_parse(rom, sizeof(rom), 0, ORACLE_A, &l) == 0 && l.max_p == 4);
    assert(gfinit_pll_parse(rom, sizeof(rom), 0, ORACLE_B, &l) == 0 && l.max_p == 5);
    put(ROM_HEADER + 2 * ROM_ENTRY + ORACLE_ROM_REG, 0, 4);
    assert(gfinit_pll_parse(rom, sizeof(rom), 0, ORACLE_B, &l) == 0 && l.max_p == 3);
    sequence_test(0, true); sequence_test(0, false);
    sequence_test(1, true); sequence_test(1, false);
    gf2_host_exit(0);
}
