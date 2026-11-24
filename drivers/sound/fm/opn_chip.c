/* AV_HAL_DESIGN, addendum 2 §§2,5. PC9800Bible §2-13 bus waits;
 * Yamaha YM2608 Application Manual §2-8-3 and BSD ymfm (C0 findings). */
#include "opn_chip.h"
#include "fm.h"
#include "io.h"

static unsigned banks(const struct opn_chip *c)
{
    return c->model == OPN_PROBE_OPNA ? 2 : c->model == OPN_PROBE_OPN;
}

static int ready(void)
{
    for (unsigned n = 0; n < 10000; ++n)
        if (!(inp(OPN_STATUS) & OPN_BUSY))
            return 1;
    return 0;
}

/* Serialize both halves of the selected bank's address/data transaction. */
static int opn_write_checked(unsigned bank, u8 reg, u8 value)
{
    unsigned flags = irq_save();
    int ok = ready();
    if (ok) {
        outp(bank ? OPNA_BANK1_ADDR : OPN_ADDR, reg);
        io_wait_n(6);
        ok = ready();
        if (ok) {
            outp(bank ? OPNA_BANK1_DATA : OPN_DATA, value);
            io_wait_n(20);
        }
    }
    irq_restore(flags);
    return ok;
}

static int chip_write(void *ctx, const SND2_RegW *w, u32 n)
{
    struct opn_chip *c = ctx;
    unsigned count = banks(c);
    if (!count || n > 256 || (!w && n))
        return OS32_ERR_INVAL;
    for (u32 i = 0; i < n; ++i)
        if (w[i].bank >= count)
            return OS32_ERR_INVAL;
    for (u32 i = 0; i < n; ++i) {
        u8 reg = w[i].reg, value = w[i].val;
        if (!w[i].bank) {
            if (reg == 0x0e || reg == 0x0f)
                continue;
            if (reg == SSG_REG_MIXER)
                value = (value & 0x3f) | SSG_MIXER_IO_FLAG;
            if (reg == 0x29 && count == 2)
                value &= ~3u;
            if (reg == OPN_REG_TIMER_CTRL && count == 1 && c->shared_irq)
                value &= ~0x0cu;
        }
        if (!opn_write_checked(w[i].bank, reg, value))
            return i;
    }
    return n;
}

static int chip_read(void *ctx, u32 bank, u32 what, u32 reg)
{
    if (bank >= banks(ctx) || (what != 1 && what != 2))
        return OS32_ERR_INVAL;
    unsigned addr = bank ? OPNA_BANK1_ADDR : OPN_ADDR;
    if (what == 1)
        return inp(addr);
    if (reg > 255)
        return OS32_ERR_INVAL;
    unsigned flags = irq_save();
    int value = OS32_ERR_IO;
    if (ready()) {
        outp(addr, reg);
        io_wait_n(6);
        if (ready())
            value = inp(bank ? OPNA_BANK1_DATA : OPN_DATA);
    }
    irq_restore(flags);
    return value;
}

static int chip_quiet(void *ctx)
{
    unsigned count = banks(ctx);
    if (!count)
        return OS32_ERR_IO;
    for (unsigned bank = 0; bank < count; ++bank)
        for (unsigned ch = 0; ch < 3; ++ch)
            if (!opn_write_checked(0, OPN_REG_KEY_ONOFF, bank * 4 + ch))
                return OS32_ERR_IO;

    for (unsigned bank = 0; bank < count; ++bank)
        for (unsigned slot = 0; slot < 4; ++slot)
            for (unsigned ch = 0; ch < 3; ++ch)
                if (!opn_write_checked(bank, OPN_REG_TL + 4 * slot + ch, OPN_TL_MAX))
                    return OS32_ERR_IO;
    for (unsigned ch = 0; ch < 3; ++ch)
        if (!opn_write_checked(0, SSG_REG_VOL_A + ch, 0))
            return OS32_ERR_IO;
    if (!opn_write_checked(0, SSG_REG_MIXER, SSG_MIXER_IO_FLAG | SSG_MIXER_ALL_OFF) ||
        !opn_write_checked(0, OPN_REG_TIMER_CTRL, OPN_TIMER_STOP))
        return OS32_ERR_IO;
    if (count == 2 && (!opn_write_checked(0, 0x10, 0xbf) ||
                       !opn_write_checked(1, 0, 1) || !opn_write_checked(1, 0, 0)))
        return OS32_ERR_IO;
    return ready() ? 0 : OS32_ERR_IO;
}

static int chip_close(void *ctx)
{
    struct opn_chip *c = ctx;
    if (banks(c) == 2)
        outp(OPNA_ID_PORT, c->saved_id);
    return 0;
}

static int chip_open(void *ctx)
{
    struct opn_chip *c = ctx;
    if (banks(c) == 2) {
        c->saved_id = inp(OPNA_ID_PORT);
        outp(OPNA_ID_PORT, c->saved_id | 1);
    }
    int rc = chip_quiet(c);
    if (rc < 0)
        chip_close(c);
    return rc;
}

static int chip_caps(void *ctx, SND2_ChipCaps *out)
{
    struct opn_chip *c = ctx;
    unsigned count = banks(c);
    if (!count || !out)
        return OS32_ERR_INVAL;
    int opna = count == 2;
    *out = (SND2_ChipCaps){
        .size = sizeof(*out), .kind = SND2_KIND_FM,
        .chip = opna ? SND2_CHIP_OPNA : SND2_CHIP_OPN,
        .clock_hz = opna ? OPNA_CLOCK_HZ : OPN_CLOCK_HZ,
        .base_port = OPN_ADDR, .id_port = opna ? OPNA_ID_PORT : 0,
        .banks = count, .fm_ch = count * 3, .ssg_ch = 3,
        .rhythm_ch = opna ? 6 : 0,
        .flags = SND2_CHF_PROBED | (opna ? SND2_CHF_RHYTHM : 0) |
                 ((opna || !c->shared_irq) ? SND2_CHF_TIMER : 0)
    };
    return 0;
}

const SndChipBackend opn_chip_backend = {
    .abi = SND_HAL2_ABI, .caps = chip_caps, .open = chip_open, .close = chip_close,
    .write = chip_write, .read = chip_read, .quiet = chip_quiet
};
