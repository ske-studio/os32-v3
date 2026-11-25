#include "boot_token.h"
#include "lan_boot.h"
#include "kboot_info.h"
#include "kernel_boot.h"
#include "ppc_oea.h"

void platform_boot_token(u8 out[16])
{
    const struct pm_kboot_info *info = ppc_boot_info();
    struct pm_gmac_boot boot = info->gmac;
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    pm_gmac_boot_id(&boot, ((u64)hi << 32) | lo, ppc_read_pvr());
    for (unsigned int i = 0; i < 16; ++i) out[i] = boot.boot_id[i];
}
