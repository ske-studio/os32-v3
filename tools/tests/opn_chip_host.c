#include "opn_chip.h"

static int locked, bad, stuck_after = -1, poll_count, inputs, writes;
static unsigned id = 0xca, id_reads, id_writes, latch[2], flags, timer;
static SND2_RegW logbuf[256];
static unsigned regs[2][256];

unsigned irq_save(void) { int old = locked; locked = 1; return old; }
void irq_restore(unsigned old) { locked = old; }
void io_wait_n(int n) { if (!locked || (n != 6 && n != 20)) bad = 1; }

unsigned inp(unsigned port)
{
    ++inputs;
    switch (port) {
    case 0xa460: ++id_reads; return id;
    case 0x188:
        if (writes == stuck_after) { ++poll_count; return 0x80; }
        return flags;
    case 0x18c: return 0x48;
    case 0x18a: case 0x18e: {
        unsigned b = port == 0x18e;
        if (!locked) bad = 1;
        return regs[b][latch[b]];
    }
    default: bad = 1; return 0;
    }
}

void outp(unsigned port, unsigned val)
{
    unsigned b = port == 0x18c || port == 0x18e;
    if (port == 0xa460) { ++id_writes; id = val; return; }
    if (!locked) bad = 1;
    if (port == 0x188 || port == 0x18c) { latch[b] = val; return; }
    if ((port != 0x18a && port != 0x18e) || writes >= 256) { bad = 1; return; }
    unsigned r = latch[b];
    regs[b][r] = val;
    logbuf[writes++] = (SND2_RegW){b, r, val};
    if (b == 0 && r == 0x27) {
        timer = val;
        flags &= ~(val >> 4);
    }
}

static int next(int *i, int b, int r, int val)
{
    if (*i >= writes) return 0;
    SND2_RegW w = logbuf[(*i)++];
    return w.bank == b && w.reg == r && w.val == val;
}

static int quiet_trace(int banks)
{
    int i = 0;
    if (banks == 2 && !next(&i, 0, 0x29, 0xa4)) return 0;
    for (int ch = 0; ch < banks * 3; ++ch)
        if (!next(&i, 0, 0x28, ch < 3 ? ch : ch + 1)) return 0;
    for (int b = 0; b < banks; ++b)
        for (int r = 0x40; r <= 0x4e; ++r)
            if ((r & 3) != 3 && !next(&i, b, r, 0x7f)) return 0;
    for (int r = 8; r <= 10; ++r)
        if (!next(&i, 0, r, 0)) return 0;
    if (!next(&i, 0, 7, 0xbf) || !next(&i, 0, 0x27, 0x30)) return 0;
    if (banks == 2 && (!next(&i, 0, 0x10, 0xbf) ||
        !next(&i, 1, 0, 1) || !next(&i, 1, 0, 0))) return 0;
    return i == writes;
}

static int cases(void)
{
    const SndChipBackend *be = &opn_chip_backend;
    struct opn_chip c = {.model = OPN_PROBE_OPN};
    SND2_ChipCaps caps;
    SND2_RegW w[] = {{0,7,0x75}, {0,14,9}, {0,15,9}, {0,0x29,0xff}, {1,7,0xc3}};
    if (be->write(&c, w, 5) != OS32_ERR_INVAL || inputs || writes) return 1;
    if (be->open(&c) || !quiet_trace(1) || be->close(&c) || id_reads || id_writes) return 2;
    writes = 0;
    if (be->write(&c,w,4) != 4 || writes != 2) return 3;
    if (regs[0][7] != 0xb5 || regs[0][0x29] != 0xff) return 4;
    SND2_RegW control = {0,0x27,5};
    if (be->write(&c,&control,1) != 1) return 5;
    if ((timer & 5) == 5) flags |= 1; /* simulated timer A overflow */
    if (be->read(&c,0,1,0) != 1) return 6;
    control.val = 0x10;
    if (be->write(&c,&control,1) != 1 || be->read(&c,0,1,0)) return 7;
    c.shared_irq = 1;
    control.val = 0xff;
    if (be->write(&c,&control,1) != 1 || timer != 0xf3 || be->caps(&c,&caps) ||
        caps.flags != SND2_CHF_PROBED || caps.clock_hz != 3993600) return 8;
    c.model = OPN_PROBE_OPNA;
    writes = 0;
    regs[0][0x29] = c.irq_shadow = 0xa7; /* previous owner enabled both IRQs */
    if (be->open(&c) || !quiet_trace(2) || id != 0xcb) return 9;
    control.val = 0x0f;
    if (be->write(&c,&control,1) != 1 || timer != 0x0f ||
        regs[0][0x29] != 0xa4 || c.irq_shadow != 0xa4) return 19;
    if (be->caps(&c,&caps) || caps.clock_hz != 7987200 || caps.fm_ch != 6 ||
        caps.banks != 2 || caps.flags != 13 || caps.adpcm_bytes) return 10;
    if (be->write(&c,w,5) != 5 || regs[0][0x29] != 0xfc || c.irq_shadow != 0xfc) return 11;
    if (be->read(&c,1,1,0) != 0x48 || be->read(&c,1,2,7) != 0xc3 ||
        be->read(&c,0,2,7) != 0xb5) return 12;
    int before = inputs;
    if (be->read(&c,2,1,0) != OS32_ERR_INVAL || be->read(&c,0,3,0) != OS32_ERR_INVAL ||
        be->read(&c,0,2,256) != OS32_ERR_INVAL || be->write(&c,w,257) != OS32_ERR_INVAL ||
        inputs != before) return 13;
    if (be->close(&c) || id != 0xca || id_reads != 1 || id_writes != 2) return 14;
    stuck_after = writes + 1;
    if (be->write(&c,w,5) != 3 || poll_count != 10000 || locked) return 15;
    if (be->read(&c,0,2,0) != OS32_ERR_IO || be->open(&c) != OS32_ERR_IO ||
        id != 0xca || locked) return 16;
    writes = 0;
    stuck_after = 38; /* BUSY begins after the final OPNA quiet write */
    if (be->quiet(&c) != OS32_ERR_IO || writes != 38) return 17;
    c.model = OPN_PROBE_UNKNOWN;
    before = inputs;
    if (be->open(&c) != OS32_ERR_IO || inputs != before || bad || locked) return 18;
    return 0;
}

void _start(void)
{
    int rc = cases();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
