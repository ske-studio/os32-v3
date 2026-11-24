/* PC-98 0188h probe (SYNTH_OPNA_DESIGN §6). Bank 1 may not exist. */
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
    if (ready_status() < 0)
        return 0;
    outp(OPN_ADDR, reg);
    io_wait_n(6);
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

enum opn_probe_result opn_probe(void)
{
    /* An open bus also has BUSY set. Distinguish repeated FF before polling. */
    unsigned first = inp(OPN_STATUS);
    unsigned second = inp(OPN_STATUS);
    if (first == 0xff && second == 0xff)
        return OPN_PROBE_ABSENT;

    int status = ready_status();
    if (status < 0 || ready_status() != status)
        return OPN_PROBE_UNKNOWN;

    outp(OPN_ADDR, 0xff);
    io_wait_n(6);
    unsigned id = inp(OPN_DATA);
    if (inp(OPN_DATA) != id || !stop_and_clear())
        return OPN_PROBE_UNKNOWN;
    if (id == 1)
        return OPN_PROBE_OPNA;

    /* Shortest period. Verify a fresh transition, then stop even on timeout. */
    int rose = write_reg(OPN_REG_TIMER_B, 0xff) &&
               write_reg(OPN_REG_TIMER_CTRL, TIMER_B_START_ENABLE) &&
               timer_b_rose();
    if (!stop_and_clear() || !rose)
        return OPN_PROBE_UNKNOWN;
    return OPN_PROBE_OPN;
}
