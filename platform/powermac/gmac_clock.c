/* NetBSD sys/arch/macppc/pci/uninorth.c:135-142: native clock bit. */
#include "lan_boot.h"

#ifdef PM_GMAC_CLOCK_FAKE
u32 pm_clock_read(u32 va);
void pm_clock_write(u32 va, u32 value);
#else
static u32 pm_clock_read(u32 va)
{
    u32 value;
    __asm__ volatile("lwzx %0,0,%1; eieio" : "=r"(value) : "r"(va) : "memory");
    return value;
}

static void pm_clock_write(u32 va, u32 value)
{
    __asm__ volatile("stwx %0,0,%1; eieio; sync" : : "r"(value), "r"(va) : "memory");
}
#endif

int pm_gmac_clock(const struct pm_gmac_boot *b, u32 clock_va)
{
    u32 value;
    if (!b || b->profile != PM_GMAC_PROFILE_UNINORTH || !clock_va)
        return LAN_EUNSUPPORTED;
    value = pm_clock_read(clock_va);
    pm_clock_write(clock_va, value | 2u);
    if (!(pm_clock_read(clock_va) & 2u)) return LAN_EIO;
    return LAN_OK;
}
