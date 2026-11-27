/* PC-98 OPN probe (SYNTH_OPNA_DESIGN §6). Bank 1 may not exist. */
#include "opn_probe.h"
#include "fm.h"
#include "io.h"

#define POLL_LIMIT 10000
#define TIMER_FLAGS 0x03
#define TIMER_B_FLAG 0x02
#define TIMER_B_START_ENABLE 0x0a

/* Return a settled status, or -1 on timeout/invalid bus response. */
static int ready_status(unsigned base)
{
    for (int i = 0; i < POLL_LIMIT; ++i) {
        unsigned status = inp(base);
        if (!(status & OPN_BUSY))
            return (status & ~TIMER_FLAGS) ? -1 : (int)status;
    }
    return -1;
}

static int write_reg(unsigned base, uchar reg, uchar value)
{
    int before = ready_status(base);
    if (before < 0)
        return 0;
    outp(base, reg);
    int after = ready_status(base);
    if (after < 0)
        return 0;
    io_wait_n(20);
    outp(base + 2, value);
    io_wait_n(20);
    return 1;
}

static int stop_and_clear(unsigned base)
{
    return write_reg(base, OPN_REG_TIMER_CTRL, OPN_TIMER_STOP) && ready_status(base) == 0;
}

static int timer_b_rose(unsigned base)
{
    for (int i = 0; i < POLL_LIMIT; ++i) {
        unsigned status = inp(base);
        if (status & OPN_BUSY)
            continue;
        if (status & ~TIMER_FLAGS)
            return 0;
        if (status & TIMER_B_FLAG)
            return 1;
    }
    return 0;
}

static enum opn_probe_result probe_bank(struct fm_probe_bank *bank, int feature,
                                        unsigned char irq_shadow, int *contained)
{
    unsigned base = bank->base;
    *contained = 1;
    /* An open bus also has BUSY set. Distinguish repeated FF before polling. */
    unsigned first = inp(base);
    unsigned second = inp(base);
    if (first == 0xff && second == 0xff) {
        if (feature < 0)
            return OPN_PROBE_UNKNOWN;
        /* io_sound.md, A460h: IDs 2/4 select 0188h, 3/5 select 0288h.
         * A masked or nonresponding board still occupies its selected base. */
        switch (feature >> 4) {
        case 15: return OPN_PROBE_ABSENT;
        case 2: case 4: return base == 0x288 ? OPN_PROBE_ABSENT : OPN_PROBE_UNKNOWN;
        case 3: case 5: return base == 0x188 ? OPN_PROBE_ABSENT : OPN_PROBE_UNKNOWN;
        default: return OPN_PROBE_UNKNOWN;
        }
    }

    if ((first == 0xff) != (second == 0xff))
        return OPN_PROBE_UNKNOWN;

    int status = ready_status(base);
    if (status < 0 || ready_status(base) != status)
        return OPN_PROBE_UNKNOWN;

    outp(base, 0xff);
    io_wait_n(6);
    if (ready_status(base) < 0)
        return OPN_PROBE_UNKNOWN;
    unsigned id = inp(base + 2);
    if (inp(base + 2) != id)
        return OPN_PROBE_UNKNOWN;
    bank->chip_id = id;
    bank->chip_id_port = base + 2;
    if (!stop_and_clear(base))
        return OPN_PROBE_UNKNOWN;
    if (id == 1)
        return OPN_PROBE_OPNA;

    if (feature < 0 || (feature >> 4) != 15)
        return OPN_PROBE_UNKNOWN;

    /* Shortest period. Verify a fresh transition, then stop even on timeout. */
    if (!write_reg(base, OPN_REG_TIMER_B, 0xff) ||
        !write_reg(base, 0x29, irq_shadow & ~0x03))
        return OPN_PROBE_UNKNOWN;

    int rose = write_reg(base, OPN_REG_TIMER_CTRL, TIMER_B_START_ENABLE) &&
               timer_b_rose(base);
    int stopped = write_reg(base, OPN_REG_TIMER_CTRL, OPN_TIMER_STOP);
    if (!stopped)
        stopped = write_reg(base, OPN_REG_TIMER_CTRL, OPN_TIMER_STOP);
    int cleared = ready_status(base) == 0;
    int restored = write_reg(base, 0x29, irq_shadow) && ready_status(base) == 0;
    *contained = stopped && cleared && restored;
    if (!*contained || !rose)
        return OPN_PROBE_UNKNOWN;
    return OPN_PROBE_OPN;
}

void opn_probe_once(struct fm_probe_record *record, int busy, unsigned char irq_shadow)
{
    if (record->complete) return;
    record->complete = 1;
    record->contained = 1;
    record->bank[0].base = 0x188;
    record->bank[1].base = 0x288;
    if (busy) return;

    int feature = inp(OPNA_ID_PORT);
    if ((int)inp(OPNA_ID_PORT) == feature) {
        record->feature_port = OPNA_ID_PORT;
        record->feature_id = feature;
    } else {
        feature = -1;
    }
    for (unsigned i = 0; i < 2; ++i) {
        int contained;
        struct fm_probe_bank *bank = &record->bank[i];
        bank->model = probe_bank(bank, feature, irq_shadow, &contained);
        if (!contained) {
            record->contained = 0;
            break;
        }
        /* PC-9801-86 IDs 4/5 select the primary/alternate FM address.
         * The FIFO control register is independent of the FM address. */
        if (bank->model == OPN_PROBE_OPNA && feature >= 0 &&
            (unsigned)(feature >> 4) == 4 + i && inp(0xa468) != 0xff)
            record->fifo_port = 0xa468;
    }
}
