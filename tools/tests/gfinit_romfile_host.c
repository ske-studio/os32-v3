/* Diagnostic only: run gfinit_decode_rom on a private ROM copy (GF_ROM_HEADER
 * is a generated array outside the repo, see tools/gfinit_rom_decode.py).
 * Prints counters and the failing pc, never ROM bytes. */
#define _start gf_profile_entry
#include "recover_gf_host.c"
#undef _start
#include "../../kdebug/gfinit_pll.c"
#include "../../kdebug/gfinit_decode.c"
#include GF_ROM_HEADER
static struct gfinit_decode decoded;
static void number(u32 x)
{
    char b[16]; u32 n = 0;
    do { b[n++] = (char)('0' + x % 10); x /= 10; } while (x);
    while (n) { char a[2] = {b[--n], 0}; print(a); }
}
static void field(const char *name, u32 value) { print(name); number(value); }
void _start(void)
{
    gf_fixture_init();
    int rc = gfinit_decode_rom(gf_private_rom, sizeof(gf_private_rom), &gf_fixture_profile, &decoded);
    field("decode rc=", rc ? 1U : 0U);
    field(" size=", (u32)sizeof(gf_private_rom));
    field(" insns=", decoded.instruction_count);
    field(" schedule=", decoded.schedule_count);
    field(" nodes=", decoded.plan.node_count);
    field(" error_pc=", decoded.error_pc);
    field(" error=", decoded.error);
    print("\n");
    /* PLL limits table at the fixed GF_INIT_PLL_TABLE: header fields, entry keys,
     * and the parse result per register (structure only, as nvbios prints). */
    const u8 *t = gf_private_rom + GF_INIT_PLL_TABLE;
    field("pll table=", GF_INIT_PLL_TABLE); field(" version=", t[0]); field(" header=", t[1]);
    field(" entry=", t[2]); field(" count=", t[3]); print("\n");
    for (u32 i = 0; i < t[3] && i < 16 && t[1] + (i + 1) * t[2] < 0x400; i++) {
        const u8 *e = t + t[1] + i * t[2];
        field("pll entry ", i); field(" key=", (u32)e[0] | (u32)e[1] << 8 | (u32)e[2] << 16 | (u32)e[3] << 24);
        print("\n");
    }
    static const u32 regs[] = {GF_PLL_CORE, GF_PLL_MEMORY, GF_PLL_VIDEO};
    for (u32 i = 0; i < 3; i++) {
        struct gfinit_pll_limits l;
        int prc = gfinit_pll_parse(gf_private_rom, sizeof(gf_private_rom), GF_INIT_PLL_TABLE, regs[i], &l);
        field("pll reg=", regs[i]); field(" parse=", prc ? 1U : 0U);
        if (!prc) { field(" ref=", l.ref); field(" max_p=", l.max_p); }
        print("\n");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
