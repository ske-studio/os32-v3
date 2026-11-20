#ifndef PLATFORM_IDE_BUS_H
#define PLATFORM_IDE_BUS_H

#include "io.h"
#include "platform_ide_ports.h"

/* Reuse the ATA protocol model behind a simulated big-endian Data window. */
extern unsigned bank, mapped_banks, absent_ctl, scalar_reads;
static inline int mapped(void) { return (mapped_banks >> bank) & 1; }
static inline void ide_bus_init(void) { }
static inline void ide_bus_select(int n) { bank = n; }
static inline u8 ide_bus_in(int reg) { return mapped() ? inp(IDE_DATA + 2 * reg) : 0xff; }
static inline void ide_bus_out(int reg, u8 v) { if (mapped()) outp(IDE_DATA + 2 * reg, v); }
static inline u8 ide_bus_alt(void) { return mapped() ? inp(IDE_ALT_STATUS) : 0xff; }
static inline void ide_bus_ctl(u8 v)
{
    if (!mapped()) {
        absent_ctl++;
        return;
    }
    outp(IDE_DEV_CTRL, v);
}

static inline u16 raw_be_data(void)
{
    return mapped() ? __builtin_bswap16(inpw(IDE_DATA)) : 0xffff;
}

static inline u16 ide_bus_data_word(void)
{
    scalar_reads++;
    return __builtin_bswap16(raw_be_data());
}

static inline void ide_bus_data_read(void *dst, u32 words)
{
    u8 *bytes = dst;
    for (u32 i = 0; i < words; i++) {
        u16 raw = raw_be_data();
        *bytes++ = raw >> 8;
        *bytes++ = raw;
    }
}

static inline void ide_bus_data_write(const void *src, u32 words)
{
    const u8 *bytes = src;
    if (!mapped()) return;
    for (u32 i = 0; i < words; i++, bytes += 2) {
        u16 raw = ((u16)bytes[0] << 8) | bytes[1];
        outpw(IDE_DATA, __builtin_bswap16(raw));
    }
}
#endif
