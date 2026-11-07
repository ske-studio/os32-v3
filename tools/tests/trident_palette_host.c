#include "trident_device_fake.h"
static struct trident_fake fake;
static struct trident_device dev;
static u8 storage[TRI_FAKE_REAL_BYTES];
static struct trident_fake_event trace[TRI_DAC_COLORS * 15 + 7];
static u8 rgb[TRI_DAC_BYTES], expected[TRI_DAC_BYTES];
static PaletteEntry system_colors[16];
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
const PaletteEntry *palette_get_all(void) { return system_colors; }
int trident_write8(struct trident_device *d, enum trident_space s, u32 off, u8 v)
{ return d->io->write8(d->io_opaque, s, off, v); }
int trident_read8(struct trident_device *d, enum trident_space s, u32 off, u8 *v)
{ return d->io->read8(d->io_opaque, s, off, v); }
/* Same AR flip-flop seam as trident_pc98init_host.c and restore_host.c. */
int trident_ar_write(struct trident_device *d, u16 base, u8 i, u8 v)
{
    u8 unused;
    int rc = trident_read8(d, TRI_SPACE_REGS, base + 6, &unused);
    if (rc) return rc;
    rc = trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, i);
    if (rc) return rc;
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, v);
}
static int reset(u8 depth, u16 base)
{
    CHECK(!trident_fake_reset(&fake, storage, sizeof(storage), trace,
                              sizeof(trace)/sizeof(trace[0])));
    trident_fake_bind(&fake, &dev);
    dev.overscan_valid = false;
    dev.overscan = 0;
    dev.mode.selected.depth = depth;
    dev.cur_vga_base = base;
    dev.saved.vga_base = base == TRI_CR_COLOR ? TRI_CR_MONO : TRI_CR_COLOR;
    fake.regs[TRI_MISC_WRITE] = base == TRI_CR_COLOR;
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) {
        fake.dac[i] = 0xA5;
        (&dev.dac[0][0])[i] = 0xA5;
    }
    return 0;
}
/* Literal trace oracle: five byte OUTs, each followed by two ST01 reads.
 * Expected values are constructed independently from the design formulas. */
static int dac_trace(u32 first, u32 count)
{
    CHECK(!fake.trace_overflow && fake.trace_count >= count * 15);
    CHECK(!fake.writes[1] && !fake.writes[2]);
    for (u32 i = 0; i < count; ++i) {
        for (u32 j = 0; j < 5; ++j) {
            u32 k = i * 15 + j * 3;
            u32 off = j == 0 ? TRI_DAC_MASK : j == 1 ? TRI_DAC_WRITE_INDEX : TRI_DAC_DATA;
            u8 value = j == 0 ? 255 : j == 1 ? (u8)(first+i) : expected[(first+i)*3+j-2];
            CHECK(trace[k].space == TRI_SPACE_REGS && trace[k].offset == off &&
                  trace[k].value == value && trace[k].width == 1 && trace[k].write && !trace[k].rc);
            for (u32 n = 1; n <= 2; ++n)
                CHECK(trace[k+n].space == TRI_SPACE_REGS && trace[k+n].offset == dev.cur_vga_base+6u &&
                      trace[k+n].width == 1 && !trace[k+n].write && !trace[k+n].rc);
        }
    }
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) {
        u8 value = i >= first*3 && i < (first+count)*3 ? expected[i] : 0xA5;
        CHECK(fake.dac[i] == value && (&dev.dac[0][0])[i] == value);
    }
    return 0;
}
static int overscan_trace(u32 start, u8 index)
{
    const u32 offsets[] = {dev.cur_vga_base+6u, TRI_AR_INDEX, dev.cur_vga_base+6u,
                           TRI_AR_INDEX, TRI_AR_INDEX, dev.cur_vga_base+6u, TRI_AR_INDEX};
    const u8 writes[] = {0,1,0,1,1,0,1};
    const u8 values[] = {0,0,0,0x11,index,0,0x20};
    CHECK(fake.trace_count == start + 7 && !fake.trace_overflow);
    CHECK(fake.ar[0x11] == index);
    for (u32 i = 0; i < 7; ++i)
        CHECK(trace[start+i].space == TRI_SPACE_REGS && trace[start+i].offset == offsets[i] &&
              trace[start+i].write == writes[i] && trace[start+i].width == 1 &&
              !trace[start+i].rc && (!writes[i] || trace[start+i].value == values[i]));
    return 0;
}
static u8 hal6(u8 value) { return (value & 15u) * 4u + (value & 15u) / 4u; }
static int defaults(void)
{
    const u8 depths[] = {8,15,16,24,24};
    const u32 counts[] = {256,32,64,256,256};
    for (u32 mode = 0; mode < 5; ++mode) {
        u8 depth = depths[mode];
        u32 count = counts[mode];
        CHECK(!reset(depth, mode & 1 ? TRI_CR_MONO : TRI_CR_COLOR));
        dev.mode.selected.bpp = mode == 4 ? 32 : depth == 15 ? 16 : depth;
        /* F8 addendum, xf86cmap.c:575-597, gamma=1.0. */
        for (u32 i = 0; i < count; ++i)
            for (u32 c = 0; c < 3; ++c) {
                u8 value;
                if (depth == 8) {
                    if (i < 16) {
                        const PaletteEntry *p = &system_colors[i];
                        value = hal6(c == 0 ? p->r : c == 1 ? p->g : p->b);
                    } else if (i < 232) {
                        u32 n = i-16;
                        u32 level = c == 0 ? n/36 : c == 1 ? n/6%6 : n%6;
                        value = level * 63 / 5;
                    } else value = ((8 + (i-232)*10)*63 + 127)/255;
                } else if (depth == 15) value = i*63/31;
                else if (depth == 16) value = c == 1 ? i : i < 32 ? i*63/31 : 0;
                else value = i*63/255;
                expected[i*3+c] = value;
            }
        CHECK(!tri_palette_load_default(&dev));
        CHECK(!dac_trace(0, count));
        u8 index = depth == 8 ? 16 : 0; /* only black cube entry; system is nonblack */
        CHECK(dev.overscan_valid && dev.overscan == index);
        CHECK(!overscan_trace(count*15, index));
        /* TrueColor StoreColors is a successful no-op, including its shadow. */
        if (depth != 8) {
            trident_fake_trace_reset(&fake);
            CHECK(!trident_palette(&dev, 0, 256, rgb) && !fake.trace_count);
            for (u32 i = 0; i < TRI_DAC_BYTES; ++i) {
                u8 value = i < count*3 ? expected[i] : 0xA5;
                CHECK(fake.dac[i] == value && (&dev.dac[0][0])[i] == value);
            }
        }
    }
    return 0;
}
static int record_reload(void)
{
    CHECK(!reset(8, TRI_CR_COLOR));
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) expected[i] = (i*13+7)%64;
    CHECK(!tri_palette_record(&dev, 0, 256, expected) && !fake.trace_count);
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) CHECK((&dev.dac[0][0])[i] == expected[i] && fake.dac[i] == 0xA5);
    dev.overscan_valid = true;
    dev.overscan = 73; /* reinstall preserves even a non-darkest saved index */
    CHECK(!tri_palette_reload(&dev));
    CHECK(!dac_trace(0, 256) && !overscan_trace(3840, 73));
    CHECK(!reset(8, TRI_CR_MONO));
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) expected[i] = 63;
    for (u32 c = 0; c < 3; ++c) expected[200*3+c] = expected[201*3+c] = 0;
    CHECK(!tri_palette_record(&dev, 0, 256, expected));
    CHECK(!tri_palette_reload(&dev));
    CHECK(dev.overscan_valid && dev.overscan == 201);
    CHECK(!dac_trace(0, 256) && !overscan_trace(3840, 201));
    for (u32 m = 0; m < 3; ++m) {
        const u8 depths[] = {15,16,24};
        u32 count = m == 0 ? 32 : m == 1 ? 64 : 256;
        CHECK(!reset(depths[m], TRI_CR_MONO));
        for (u32 i = 0; i < count; ++i)
            for (u32 c = 0; c < 3; ++c)
                expected[i*3+c] = m == 0 ? i*63/31 : m == 1 ?
                    (c == 1 ? i : i < 32 ? i*63/31 : 0) : i*63/255;
        CHECK(!tri_palette_reload(&dev));
        CHECK(!dac_trace(0, count) && !overscan_trace(count*15, 0));
    }
    return 0;
}
/* CMapSetOverscan: reverse full search, forward changed search, strict sum
 * ties, black retention and saved-index reinstall are distinct contracts. */
static int overscan_cases(void)
{
    const u8 initial[] = {0,0,40,40,40,40,40,40};
    const int first[] = {10,10,40,10,10,10,10,40};
    const u8 update[] = {5,5,5,0,0,5,5,5};
    const u8 want[] = {201,11,200,10,40,10,40,40};
    const bool emit[] = {true,true,true,true,false,true,false,true};
    for (u32 t = 0; t < 8; ++t) {
        CHECK(!reset(8, TRI_CR_COLOR));
        for (u32 i = 0; i < TRI_DAC_BYTES; ++i) (&dev.dac[0][0])[i] = 63;
        dev.overscan_valid = t != 0 && t != 1;
        dev.overscan = initial[t];
        if (t == 0) for (u32 c = 0; c < 3; ++c) dev.dac[200][c] = dev.dac[201][c] = 0;
        if (t == 2) for (u32 c = 0; c < 3; ++c) dev.dac[200][c] = 0;
        if (t >= 3) for (u32 c = 0; c < 3; ++c) dev.dac[40][c] = t == 4 ? 0 : t == 7 ? 21 : 42;
        /* case 5: equal changed sums choose first; case 6: equal old sum stays */
        if (t == 6) for (u32 c = 0; c < 3; ++c) dev.dac[40][c] = 21;
        for (u32 i = 0; i < 6; ++i) rgb[i] = update[t];
        CHECK(!trident_palette(&dev, first[t], 2, rgb));
        CHECK(dev.overscan_valid && dev.overscan == want[t]);
        if (emit[t]) CHECK(!overscan_trace(30, want[t]));
        else CHECK(fake.trace_count == 30);
    }
    /* Unequal component sums, rather than a single channel comparison. */
    CHECK(!reset(8, TRI_CR_COLOR));
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) (&dev.dac[0][0])[i] = 63;
    dev.dac[50][0] = 0; dev.dac[50][1] = 20; dev.dac[50][2] = 20;
    dev.dac[51][0] = 10; dev.dac[51][1] = 10; dev.dac[51][2] = 10;
    CHECK(!trident_palette(&dev, 10, 1, rgb) && dev.overscan == 51);
    return 0;
}
static int boundaries_errors(void)
{
    CHECK(!reset(8, TRI_CR_COLOR));
    dev.overscan_valid = true; dev.overscan = 0;
    dev.dac[0][0] = dev.dac[0][1] = dev.dac[0][2] = 0;
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) rgb[i] = (u8)(i*13+7);
    for (u32 c = 0; c < 3; ++c) expected[255*3+c] = hal6(rgb[c]);
    CHECK(!trident_palette(&dev, 255, 1, rgb) && fake.trace_count == 15);
    for (u32 c = 0; c < 3; ++c)
        CHECK(fake.dac[255*3+c] == expected[255*3+c] && dev.dac[255][c] == expected[255*3+c]);
    trident_fake_trace_reset(&fake);
    CHECK(trident_palette(&dev, -1, 1, rgb) == TRI_E_INVAL);
    CHECK(trident_palette(&dev, 256, 1, rgb) == TRI_E_INVAL);
    CHECK(trident_palette(&dev, 255, 2, rgb) == TRI_E_INVAL);
    CHECK(trident_palette(&dev, 0, 0, rgb) == TRI_E_INVAL);
    CHECK(trident_palette(&dev, 0, -1, rgb) == TRI_E_INVAL);
    CHECK(trident_palette(&dev, 0, 1, 0) == TRI_E_INVAL);
    CHECK(trident_palette(0, 0, 1, rgb) == TRI_E_INVAL);
    CHECK(tri_palette_record(&dev, 255, 2, rgb) == TRI_E_INVAL);
    CHECK(tri_palette_record(&dev, 0, 0, rgb) == TRI_E_INVAL);
    CHECK(tri_palette_record(0, 0, 1, rgb) == TRI_E_INVAL);
    CHECK(tri_palette_record(&dev, 0, 1, 0) == TRI_E_INVAL && !fake.trace_count);
    CHECK(tri_palette_load_default(0) == TRI_E_INVAL && tri_palette_reload(0) == TRI_E_INVAL);
    CHECK(tri_overscan(0, 1) == TRI_E_INVAL);
    dev.mode.selected.depth = 7;
    CHECK(trident_palette(&dev, 0, 1, rgb) == TRI_E_INVAL);
    CHECK(tri_palette_load_default(&dev) == TRI_E_INVAL && !fake.trace_count);
    for (u32 op = 1; op <= 37; ++op) {
        CHECK(!reset(8, TRI_CR_MONO));
        trident_fake_fail(&fake, op, -77);
        CHECK(trident_palette(&dev, 42, 2, rgb) == -77);
        CHECK(fake.trace_count == op && trace[op-1].rc == -77);
        /* Record only after all five OUT+delay groups of an entry complete. */
        for (u32 c = 0; c < 3; ++c) {
            CHECK(dev.dac[42][c] == (op > 15 ? hal6(rgb[c]) : 0xA5));
            CHECK(dev.dac[43][c] == (op > 30 ? hal6(rgb[3+c]) : 0xA5));
        }
    }
    /* Default/reload errors propagate through both DAC and overscan phases. */
    const u32 fail_ops[] = {1,15,480,481,487};
    for (u32 reload = 0; reload < 2; ++reload)
        for (u32 i = 0; i < 5; ++i) {
            CHECK(!reset(15, TRI_CR_COLOR));
            trident_fake_fail(&fake, fail_ops[i], -78);
            CHECK((reload ? tri_palette_reload(&dev) : tri_palette_load_default(&dev)) == -78);
            CHECK(fake.trace_count == fail_ops[i]);
        }
    return 0;
}
static int test(void)
{
    for (u32 i = 0; i < 16; ++i) {
        system_colors[i].r = i+1; system_colors[i].g = 15-i; system_colors[i].b = 1;
    }
    int rc = defaults(); if (rc) return rc;
    rc = record_reload(); if (rc) return rc;
    rc = overscan_cases(); if (rc) return rc;
    return boundaries_errors();
}
void _start(void)
{
    int rc = test();
    /* Print the failing line with a direct syscall, without libc. */
    if (rc) {
        char message[] = "RED line 0000\n";
        u32 n = rc;
        for (int i = 12; i >= 9; --i) { message[i] = '0' + n%10; n /= 10; }
        __asm__ volatile ("int $0x80" : : "a"(4), "b"(1), "c"(message), "d"(sizeof(message)-1) : "memory");
    }
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
