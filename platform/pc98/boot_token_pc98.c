#include "boot_token.h"
#include "arch_cpu.h"
#include "kernel_tick.h"
#include "lan_contract.h"
#include "pit.h"
#include "../../drivers/char/rtc.h"
#include "../../drivers/net/ne2000/ne2000.h"

void platform_boot_token(u8 out[16])
{
    u32 lo, hi;
    u8 mac[6] = {0}, legacy[6] = {0};
    RTC_Time rtc;
    if (arch_cpu_has_tsc())
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    else { lo = tick_count; hi = pit_read_count(); }
    e82557_get_mac(mac);
    ne2k_get_mac(legacy);
    rtc_read(&rtc);
    for (unsigned int i = 0; i < 16; ++i) {
        u8 clock = ((const u8 *)&rtc)[i % sizeof(rtc)];
        u8 t = i < 8 ? (u8)(lo >> (8 * (i % 4))) : (u8)(hi >> (8 * (i % 4)));
        out[i] = t ^ mac[i % 6] ^ legacy[(i + 1) % 6] ^ clock ^ (u8)(i * 29u);
    }
}
