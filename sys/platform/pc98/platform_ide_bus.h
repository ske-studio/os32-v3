#ifndef PLATFORM_IDE_BUS_H
#define PLATFORM_IDE_BUS_H

#include "io.h"
#include "pc98.h"
#include "platform_ide_ports.h"
extern void irq_disable(unsigned int irq);
_Static_assert(IDE_BUS_OPEN8 == PC98_IO_OPEN_BUS8, "ide open bus");
static inline void ide_bus_init(void)        { irq_disable(PC98_IRQ_IDE); }
static inline void ide_bus_select(int bank)  { outp(IDE_BANK1, (unsigned)(bank ? 0x01 : 0x00)); }
static inline u8   ide_bus_in(int reg)       { return (u8)inp(IDE_DATA + 2 * reg); }
static inline void ide_bus_out(int reg, u8 v){ outp(IDE_DATA + 2 * reg, (unsigned)v); }
static inline u8   ide_bus_alt(void)         { return (u8)inp(IDE_ALT_STATUS); }
static inline void ide_bus_ctl(u8 v)         { outp(IDE_DEV_CTRL, (unsigned)v); }
static inline u16  ide_bus_data_word(void)   { return (u16)inpw(IDE_DATA); }

/* PC-98 permits unaligned 16-bit loads and stores. */
typedef u16 ide_unaligned_word __attribute__((aligned(1), may_alias));

static inline void ide_bus_data_read(void *dst, u32 words)
{
    ide_unaligned_word *out = dst;
    for (u32 w = 0; w < words; w++) *out++ = (u16)inpw(IDE_DATA);
}

static inline void ide_bus_data_write(const void *src, u32 words)
{
    const ide_unaligned_word *in = src;
    for (u32 w = 0; w < words; w++) outpw(IDE_DATA, (unsigned)*in++);
}

#endif
