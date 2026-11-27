/* PC-98 OPN probe (SYNTH_OPNA_DESIGN §6). Bank 1 may not exist. */
#include "opn_probe.h"
#include "fm.h"
#include "io.h"

#define POLL_LIMIT 10000
#define TIMER_FLAGS 0x03
#define TIMER_B_FLAG 0x02
#define TIMER_B_START_ENABLE 0x0a

/* Return a settled status, or -1 on timeout/invalid bus response. */
static int ready_status(void)
{
    for (int i = 0; i < POLL_LIMIT; ++i) {
        unsigned status = inp(OPN_STATUS);
        if (!(status & OPN_BUSY))
            return (status & ~TIMER_FLAGS) ? -1 : (int)status;
    }
    return -1;
}

static int write_reg(uchar reg, uchar value)
{
    int before = ready_status();
    if (before < 0)
        return 0;
    outp(OPN_ADDR, reg);
    int after = ready_status();
    if (after < 0)
        return 0;
    io_wait_n(20);
    outp(OPN_DATA, value);
    io_wait_n(20);
    return 1;
}

static int stop_and_clear(void)
{
    return write_reg(OPN_REG_TIMER_CTRL, OPN_TIMER_STOP) && ready_status() == 0;
}

static int timer_b_rose(void)
{
    for (int i = 0; i < POLL_LIMIT; ++i) {
        unsigned status = inp(OPN_STATUS);
        if (status & OPN_BUSY)
            continue;
        if (status & ~TIMER_FLAGS)
            return 0;
        if (status & TIMER_B_FLAG)
            return 1;
    }
    return 0;
}

static enum opn_probe_result probe_primary(unsigned char irq_shadow,
                                           unsigned char *contained)
{
    unsigned first = inp(OPN_STATUS);
    unsigned second = inp(OPN_STATUS);
    if (first == 0xff && second == 0xff)
        return OPN_PROBE_ABSENT;

    int status = ready_status();
    if (status < 0 || ready_status() != status)
        return OPN_PROBE_UNKNOWN;

    *contained = 0;
    outp(OPN_ADDR, 0xff);
    io_wait_n(6);
    if (ready_status() < 0) return OPN_PROBE_UNKNOWN;
    unsigned id = inp(OPN_DATA);
    if (inp(OPN_DATA) != id || !stop_and_clear())
        return OPN_PROBE_UNKNOWN;
    if (id == 1) {
        *contained = 1;
        return OPN_PROBE_OPNA;
    }

    if (!write_reg(OPN_REG_TIMER_B, 0xff) ||
        !write_reg(0x29, irq_shadow & ~0x03))
        return OPN_PROBE_UNKNOWN;

    int rose = write_reg(OPN_REG_TIMER_CTRL, TIMER_B_START_ENABLE) &&
               timer_b_rose();
    int stopped = write_reg(OPN_REG_TIMER_CTRL, OPN_TIMER_STOP);
    if (!stopped)
        stopped = write_reg(OPN_REG_TIMER_CTRL, OPN_TIMER_STOP);
    int cleared = ready_status() == 0;
    int restored = write_reg(0x29, irq_shadow) && ready_status() == 0;
    *contained = stopped && cleared && restored;
    if (!*contained || !rose) return OPN_PROBE_UNKNOWN;
    return OPN_PROBE_OPN;
}

static int open_bus(unsigned base)
{
    return inp(base) == 0xff && inp(base + 2) == 0xff &&
           inp(base) == 0xff && inp(base + 2) == 0xff;
}

void opn_probe_once(struct fm_probe_record *record, int busy, unsigned char irq_shadow,
                    int (*now_us)(u32 *, u32 *))
{
    if (record->complete) return;
    record->complete = 1;
    record->contained = 1;
    record->bank[0].base = 0x188;
    record->bank[1].base = 0x288;

    (void)now_us; /* Kept in the version 4 FM module interface. */
    record->native_model = probe_primary(irq_shadow, &record->contained);
    if (busy || !record->contained) return;

    if (open_bus(0x288)) record->bank[1].model = OPN_PROBE_ABSENT;
    unsigned feature = inp(OPNA_ID_PORT);
    unsigned board = feature >> 4;
    int stable = inp(OPNA_ID_PORT) == feature;
    /* A460h: 7 = integrated CS4231 without FM, 15 = no ID or 26-class. */
    int primary_may_be_absent = board == 7 || board == 15;
    record->bank[0].model = record->native_model;
    if (record->native_model == OPN_PROBE_ABSENT &&
        (!open_bus(OPN_ADDR) || !stable || !primary_may_be_absent))
        record->bank[0].model = OPN_PROBE_UNKNOWN;
    if (record->native_model == OPN_PROBE_OPN)
        record->bank[0].model = OPN_PROBE_PRESENT;
    if (record->native_model == OPN_PROBE_OPNA) {
        record->bank[0].chip_id_port = OPN_DATA;
        record->bank[0].chip_id = 1;
        if (stable && (board == 2 || board == 4)) {
            record->feature_port = OPNA_ID_PORT;
            record->feature_id = feature;
            if (board == 4 && inp(0xa468) != 0xff) record->fifo_port = 0xa468;
        }
    }
}
