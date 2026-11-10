#include "platform/powermac/lan_boot.h"
static u32 reg, reads, writes, bad_va, reject;
u32 pm_clock_read(u32 va) { if (va != 0x1000u) bad_va++; reads++; return reg; }
void pm_clock_write(u32 va, u32 value) { if (va != 0x1000u) bad_va++; writes++; if (!reject) reg = value; }
static void output(const char *s) { unsigned n=0; while (s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
static int test(void)
{
    struct pm_gmac_boot b = {0};
    output("REACHED host\n");
    b.profile = PM_GMAC_PROFILE_UNINORTH;
#if CASE == 0
    reg = 0xa5a50080u;
    if (pm_gmac_clock(&b, 0x1000u) != LAN_OK || reg != 0xa5a50082u || reads != 2 || writes != 1 || bad_va) { output("H:rmw\n"); return 1; }
    if (pm_gmac_clock(&b, 0x1000u) != LAN_OK || reg != 0xa5a50082u) { output("H:already\n"); return 1; }
#elif CASE == 1
    reject = 1;
    if (pm_gmac_clock(&b, 0x1000u) != LAN_EIO || reads != 2 || writes != 1) { output("H:readback\n"); return 1; }
#elif CASE == 2
    b.profile = 99;
    if (pm_gmac_clock(&b, 0x1000u) != LAN_EUNSUPPORTED || reads || writes) { output("H:profile\n"); return 1; }
#elif CASE == 3
    if (pm_gmac_clock(&b, 0) != LAN_EUNSUPPORTED || reads || writes || pm_gmac_clock(0, 0x1000u) != LAN_EUNSUPPORTED || reads || writes) { output("H:alias\n"); return 1; }
#endif
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
