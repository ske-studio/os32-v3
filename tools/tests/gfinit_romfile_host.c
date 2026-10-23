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
    int rc = gfinit_decode_rom(gf_private_rom, sizeof(gf_private_rom), &recover_gf_template, &decoded);
    field("decode rc=", rc ? 1U : 0U);
    field(" size=", (u32)sizeof(gf_private_rom));
    field(" insns=", decoded.instruction_count);
    field(" schedule=", decoded.schedule_count);
    field(" nodes=", decoded.plan.node_count);
    field(" error_pc=", decoded.error_pc);
    field(" error=", decoded.error);
    print("\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
