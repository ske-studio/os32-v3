#pragma once
#include "ide_boot.h"
#include "ppc_boot.h"
#include "ppc_mmio16.h"

/* KeyLargo and Kauai taskfile layout: docs/ppc/ata.md §§2–3. */
static u32 ide_window;
static inline void ide_bus_init(void) { ide_window = 0; }
static inline void ide_bus_select(int bank) { ide_window = pm_ide_base[bank]; }
static inline u8 ide_bus_in(int reg)
{
    return ide_window ? ppc_load8(ide_window + 16 * reg) : IDE_BUS_OPEN8;
}
static inline void ide_bus_out(int reg, u8 v)
{
    if (ide_window) ppc_store8(ide_window + 16 * reg, v);
}
static inline u8 ide_bus_alt(void)
{
    return ide_window ? ppc_load8(ide_window + PM_IDE_CTL) : IDE_BUS_OPEN8;
}
static inline void ide_bus_ctl(u8 v)
{
    if (ide_window) ppc_store8(ide_window + PM_IDE_CTL, v);
}
static inline u16 ide_bus_data_word(void)
{
    return ide_window ? __builtin_bswap16(ppc_load16(ide_window)) : 0xffff;
}
static inline void ide_bus_data_read(void *dst, u32 words)
{
    u8 *p = dst;
    while (words--) {
        u16 w = ide_window ? ppc_load16(ide_window) : 0xffff;
        *p++ = w >> 8;
        *p++ = w;
    }
    ppc_sync();
}
static inline void ide_bus_data_write(const void *src, u32 words)
{
    const u8 *p = src;
    if (!ide_window) return;
    while (words--) {
        ppc_store16(ide_window, ((u16)p[0] << 8) | p[1]);
        p += 2;
    }
    ppc_sync();
}
