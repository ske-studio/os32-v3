/* libc-free mode harness. Register behavior and trace belong exclusively to
 * trident_device_fake; these wrappers stand in for the separate regs.c stage.
 * Their byte sequences follow vgaHW.c:369-488,574-604,1795-1806 and
 * trident_dac.c:781-791. Full production binding is an integration-stage test.
 */
#include "trident_device_fake.h"
#define TRY(x) do { int rc = (x); if (rc) return rc; } while (0)
int trident_read8(struct trident_device *d, enum trident_space s, u32 o, u8 *v)
{ return d->io->read8(d->io_opaque, s, o, v); }
int trident_write8(struct trident_device *d, enum trident_space s, u32 o, u8 v)
{ return d->io->write8(d->io_opaque, s, o, v); }
int trident_indexed_read(struct trident_device *d, u16 p, u8 i, u8 *v)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, p, i));
    return trident_read8(d, TRI_SPACE_REGS, p + 1, v);
}
int trident_indexed_write(struct trident_device *d, u16 p, u8 i, u8 v)
{ return d->io->write16(d->io_opaque, TRI_SPACE_REGS, p, i | ((u16)v << 8)); }
static int pair(struct trident_device *d, u16 p, u8 i, u8 v)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, p, i));
    return trident_write8(d, TRI_SPACE_REGS, p + 1, v);
}
int trident_crtc_lock(struct trident_device *d, u16 b, bool lock)
{
    u8 v;
    TRY(trident_indexed_read(d, b, 0x11, &v));
    return pair(d, b, 0x11, lock ? v | 0x80 : v & 0x7F);
}
int trident_ar_write(struct trident_device *d, u16 b, u8 i, u8 v)
{
    u8 t;
    TRY(trident_read8(d, TRI_SPACE_REGS, b + 6, &t));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, i));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, v);
}
int trident_protect(struct trident_device *d, u16 b, bool on)
{
    u8 v, t;
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 1, &v));
    if (on) {
        TRY(pair(d, TRI_SR_INDEX, 0, 1));
        TRY(pair(d, TRI_SR_INDEX, 1, v | 0x20));
    } else {
        TRY(pair(d, TRI_SR_INDEX, 1, v & 0xDF));
        TRY(pair(d, TRI_SR_INDEX, 0, 3));
    }
    TRY(trident_read8(d, TRI_SPACE_REGS, b + 6, &t));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, on ? 0 : 0x20);
}
int trident_hidden_dac_write(struct trident_device *d, u8 v)
{
    u8 t;
    TRY(trident_read8(d, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, &t));
    for (u32 i = 0; i < 4; ++i)
        TRY(trident_read8(d, TRI_SPACE_REGS, TRI_DAC_MASK, &t));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_DAC_MASK, v));
    return trident_read8(d, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, &t);
}

#include "mode_expected.h"
static struct trident_fake f;
static struct trident_fake_event trace[512]; /* > full reference trace */
static u8 storage[TRI_FAKE_REAL_BYTES];
static struct trident_device d;
static void clear(void *p, u32 n)
{ for (u32 i = 0; i < n; ++i) ((u8 *)p)[i] = 0; }
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    return a == b;
}
static int setup(const struct mode_case *c)
{
    clear(&d, sizeof(d));
    TRY(trident_fake_reset(&f, storage, sizeof(storage), trace,
                            sizeof(trace) / sizeof(trace[0])));
    trident_fake_bind(&f, &d);
    for (u32 i = 0; i < 256; ++i) {
        f.sr[i] = (u8)(c->seed + i * 17);
        f.gr[i] = (u8)(c->seed + i * 13);
        f.cr_color[i] = (u8)(c->seed + i * 7);
        f.cr_mono[i] = (u8)(c->seed + i * 11);
    }
    f.cr_color[0xC0] = c->base == TRI_CR_COLOR ? c->reference : c->reference ^ 0x80;
    f.cr_mono[0xC0] = c->base == TRI_CR_MONO ? c->reference : c->reference ^ 0x80;
    f.regs[TRI_DAC_MASK] = 0xA5;
    f.regs[TRI_DAC_WRITE_INDEX] = 0x37;
    f.hidden_dac = 0x73;
    d.saved.vga_base = c->base == TRI_CR_COLOR ? TRI_CR_MONO : TRI_CR_COLOR;
    d.cur_vga_base = c->base;
    f.regs[TRI_MISC_WRITE] = c->misc;
    d.saved.valid = true;
    d.reference = 0; /* IsClearTV frequency is initially unset. */
    d.last_rc = -77;
    d.declared_bytes = 4u * 1024u * 1024u;
    d.settings.no_accel = c->noaccel;
    d.settings.ram_fallback = TRI_RAM_FALLBACK_DEFAULT;
    /* At the exact boundary MUX is off; one kHz below enables it. */
    d.settings.mux_threshold_khz = c->mux ? 25174 : 25175;
    return 0;
}
static int check_case(const struct mode_case *c)
{
    TRY(setup(c));
    TRY(tri_mode_set(&d, &tri_modes[c->mode]));
    if (!equal(d.reference, (c->reference & 0x80) | 1) || !equal(f.trace_count, c->count) ||
        f.trace_overflow || !equal(d.last_rc, (u32)-77) || !d.active ||
        d.init_done || d.preinit_done || f.pc98_initialized || f.port_writes ||
        !d.saved.valid || !equal(d.cur_vga_base, TRI_CR_COLOR)) return 1;
    for (u32 i = 0; i < c->count; ++i) {
        const struct trident_fake_event *a = &trace[i], *b = &c->events[i];
        if (!equal(a->space, b->space) || !equal(a->offset, b->offset) ||
            !equal(a->value, b->value) || !equal(a->width, b->width) ||
            !equal(a->write, b->write) || !equal(a->count, 1) || a->rc) return 2;
    }
    if (!d.mode.valid || d.mode.selected.id != c->mode + 1u ||
        d.mode.selected.display_width * (d.mode.selected.bpp / 8u) != c->pitch ||
        d.mode.no_accel != (c->noaccel || TRI_RAM_FALLBACK_DEFAULT || c->mode % 5 == 3) ||
        d.mode.mux != c->mux) return 6;
    /* Reenter with the opposite C0 bit at the now-color base. IsClearTV
     * must retain the first selection even when that selection was NTSC.
     * Check both reference and actual clock writes; no C0 read is allowed. */
    f.cr_color[0xC0] = c->reference ^ 0x80;
    f.trace_count = 0;
    TRY(tri_mode_set(&d, &tri_modes[c->mode]));
    if (!equal(d.reference, (c->reference & 0x80) | 1) ||
        !equal(f.regs[TRI_PLL_LO], c->pll_lo) ||
        !equal(f.regs[TRI_PLL_HI], c->pll_hi)) return 4;
    for (u32 i = 0; i < f.trace_count; ++i)
        if (trace[i].write && trace[i].offset == TRI_CR_COLOR &&
            trace[i].value == 0xC0) return 5;
    /* Every actual I/O, including exit Protect, must propagate its exact rc
     * and stop at that operation. This also covers helper failure returns. */
    for (u32 op = 1; c->seed == 0 && op <= c->count; ++op) {
        TRY(setup(c));
        trident_fake_fail(&f, op, -123);
        int rc = tri_mode_set(&d, &tri_modes[c->mode]);
        if (!equal(rc, (u32)-123) || !equal(f.operations, op) ||
            !equal(f.trace_count, op) || !equal(trace[op - 1].rc, (u32)-123))
            return 3;
    }
    return 0;
}
/* Candidate rejection must happen before touching any register. */
static int invalid_cases(void)
{
    TRY(setup(&cases[0]));
    struct tri_mode m = tri_modes[0], prepared;
    if (tri_mode_prepare(0, &m, &prepared) != TRI_E_INVAL ||
        tri_mode_prepare(&d, 0, &prepared) != TRI_E_INVAL ||
        tri_mode_prepare(&d, &m, 0) != TRI_E_INVAL) return 7;
    for (u32 i = 0; i < 7; ++i) {
        m = tri_modes[0];
        switch (i) {
        case 0: m.bpp = 32; break; /* depth8/bpp32 is not a format */
        case 1: m.flags |= 0x80000000u; break;
        case 2: m.hs = m.hd; break;
        case 3: m.ve = m.vt; break;
        case 4: m.ht = 801; break;
        case 5: m.dot_khz = 26000; break; /* exceeds 32kHz */
        case 6: m.dot_khz = 24000; break; /* below 31kHz */
        }
        if (tri_mode_set(&d, &m) != TRI_E_INVAL || f.operations) return 8;
    }
    d.declared_bytes = 256u*1024u;
    m = tri_modes[0];
    if (tri_mode_set(&d, &m) != TRI_E_CAP_TOO_SMALL || f.operations) return 9;
    return 0;
}
static void emit(const char *s, u32 n)
{ __asm__ volatile ("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
void _start(void)
{
    int rc = trident_mode_640(0) != TRI_E_INVAL;
    if (!rc) rc = invalid_cases();
    for (u32 i = 0; !rc && i < sizeof(cases) / sizeof(cases[0]); ++i)
        rc = check_case(&cases[i]);
    if (rc) emit("RED mode trace/rc\n", 18);
    else emit("GREEN\n", 6);
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
