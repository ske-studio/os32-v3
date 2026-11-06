#include "trident_device_fake.h"
static struct trident_fake fake;
static struct trident_device dev;
static u8 storage[TRI_FAKE_REAL_BYTES];
static struct trident_fake_event trace[TRI_DAC_COLORS * 15];
static u8 rgb[TRI_DAC_BYTES];
int trident_write8(struct trident_device *d, enum trident_space s, u32 off, u8 v)
{ return d->io->write8(d->io_opaque, s, off, v); }
int trident_read8(struct trident_device *d, enum trident_space s, u32 off, u8 *v)
{ return d->io->read8(d->io_opaque, s, off, v); }
static int test(void)
{
    if (trident_fake_reset(&fake, storage, sizeof(storage), trace, sizeof(trace)/sizeof(trace[0]))) return 1;
    trident_fake_bind(&fake, &dev);
    dev.saved.vga_base = TRI_CR_MONO;
    dev.cur_vga_base = TRI_CR_COLOR;
    fake.regs[TRI_MISC_WRITE] = 1;
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) rgb[i] = (u8)(i * 13 + 7);
    if (trident_palette(&dev, 0, 256, rgb)) return 2;
    if (fake.trace_count != TRI_DAC_COLORS * 15 || fake.trace_overflow || fake.writes[1] || fake.writes[2]) return 3;
    for (u32 i = 0; i < TRI_DAC_COLORS; ++i) {
        for (u32 j = 0; j < 5; ++j) {
            u32 k = i * 15 + j * 3;
            u32 off = j == 0 ? TRI_DAC_MASK : j == 1 ? TRI_DAC_WRITE_INDEX : TRI_DAC_DATA;
            u8 value = j == 0 ? 255 : j == 1 ? (u8)i : (u8)(((rgb[i * 3 + j - 2] & 15) << 2) | ((rgb[i * 3 + j - 2] & 15) >> 2));
            if (trace[k].space != TRI_SPACE_REGS || trace[k].offset != off || trace[k].value != value || trace[k].width != 1 || !trace[k].write || trace[k].rc) return 4;
            for (u32 n = 1; n <= 2; ++n)
                if (trace[k+n].space != TRI_SPACE_REGS || trace[k+n].offset != TRI_CR_COLOR + 6 || trace[k+n].width != 1 || trace[k+n].write || trace[k+n].rc) return 5;
        }
        for (u32 j = 0; j < 3; ++j)
            if (fake.dac[i*3+j] != (u8)(((rgb[i*3+j] & 15) << 2) | ((rgb[i*3+j] & 15) >> 2))) return 6;
    }
    trident_fake_trace_reset(&fake);
    if (trident_palette(&dev, 255, 1, rgb) || fake.trace_count != 15 || trace[3].value != 255) return 7;
    trident_fake_trace_reset(&fake);
    if (trident_palette(&dev, -1, 1, rgb) != TRI_E_INVAL || trident_palette(&dev, 256, 1, rgb) != TRI_E_INVAL || trident_palette(&dev, 255, 2, rgb) != TRI_E_INVAL || trident_palette(&dev, 0, 0, rgb) != TRI_E_INVAL || trident_palette(&dev, 0, -1, rgb) != TRI_E_INVAL || trident_palette(&dev, 0, 1, 0) != TRI_E_INVAL || fake.trace_count) return 8;
    dev.cur_vga_base = TRI_CR_MONO;
    fake.regs[TRI_MISC_WRITE] = 0;
    for (u32 op = 1; op <= 15; ++op) {
        trident_fake_trace_reset(&fake);
        trident_fake_fail(&fake, fake.operations + op, -77);
        if (trident_palette(&dev, 42, 1, rgb) != -77 || fake.trace_count != op || trace[op-1].rc != -77) return 9;
    }
    trident_fake_fail(&fake, 0, 0);
    trident_fake_trace_reset(&fake);
    if (trident_palette(&dev, 1, 1, rgb) || (trace[1].offset != TRI_CR_MONO + 6 || trace[2].offset != TRI_CR_MONO + 6)) return 10;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
