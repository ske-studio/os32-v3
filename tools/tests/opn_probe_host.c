#include "opn_probe.h"

/* Model only the two bank-zero ports. Unexpected accesses fail the case. */
enum scenario {
    FLOATING, BUSY, RESERVED, MOVING_STATUS, MOVING_ID, OPNA, OPN,
    NO_RISE, STALE_FLAG, NO_CLEAR, BUSY_AFTER_ID, BUSY_AFTER_START, INVALID_AFTER_START, BUSY_ON_RESTORE, RETRY_STOP, CASE_COUNT
};

static enum scenario mode;
static unsigned reads, latch, ids, writes, stops, starts, preset, running, flag, bad;

/* Last four bus events, oldest first. R = status, A = address,
 * W = 20 waits, D = register data. */
static unsigned trace, irq, pulse, restores, restore_busy, stop_busy, stop_delayed;

static int busy(void)
{
    return (mode == BUSY_AFTER_START && starts) || restore_busy || stop_busy;
}

static void event(unsigned char tag)
{
    trace = (trace << 8) | tag;
}

void io_wait_n(int n)
{
    event(n >= 20 ? 'W' : 'w');
}

unsigned inp(unsigned port)
{
    if (port == 0x18a) {
        if (latch != 0xff || reads < 2)
            bad = 1;
        ++ids;
        return mode == OPNA || (mode == MOVING_ID && ids == 1) ? 1 : 0xff;
    }
    event('R');
    if (port != 0x188 || ++reads > 70000)
        bad = 1;
    if (mode == FLOATING) return 0xff;
    if (mode == BUSY || (mode == BUSY_AFTER_ID && ids)) return 0x80;
    if (busy()) {
        if (stop_busy) --stop_busy;
        return 0x80;
    }
    if (starts && mode == INVALID_AFTER_START) return 0x40;
    if (pulse) { pulse = 0; return 0x80; }
    if (mode == RESERVED) return 0x40;
    if (mode == MOVING_STATUS) return reads & 1;
    if (mode == STALE_FLAG) return 2;
    if (mode == OPN && reads <= 5) return 0x80;
    if (running && mode != NO_RISE) flag = 2;
    return flag;
}

void outp(unsigned port, unsigned value)
{
    ++writes;
    if (port == 0x18a) {
        if ((trace & 0xffff) != ('R' << 8 | 'W') || pulse)
            bad = 1;
        event('D');
    }
    if (port == 0x188) {
        if (value != 0xff && (trace & 0xff) != 'R')
            bad = 1;
        event('A');
        if (starts && mode == BUSY_ON_RESTORE && value == 0x29)
            restore_busy = 1;
        if (starts && mode == RETRY_STOP && value == 0x27 && !stop_delayed) {
            stop_busy = 10000;
            stop_delayed = 1;
        }
        if (busy()) return;
        pulse = mode == OPN;
        latch = value;
    } else if (port != 0x18a || ids < 2) {
        bad = 1;
    } else if (latch == 0x27 && value == 0x30) {
        ++stops;
        running = 0;
        if (mode != NO_CLEAR) flag = 0;
    } else if (latch == 0x29) {
        if (value != (starts ? 0x83 : 0x80) || running) bad = 1;
        irq = value;
        if (starts) ++restores;
    } else if (latch == 0x26 && value == 0xff && stops) {
        preset = 1;
    } else if (latch == 0x27 && value == 0x0a && preset) {
        if (irq != 0x80) bad = 1;
        ++starts;
        running = 1;
    } else {
        bad = 1;
    }
}

static int cases(void)
{
    static const enum opn_probe_result expected[] = {
        OPN_PROBE_ABSENT, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN,
        OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_OPNA, OPN_PROBE_OPN,
        OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN,
        OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_OPN
    };
    for (mode = FLOATING; mode < CASE_COUNT; ++mode) {
        reads = latch = ids = writes = stops = starts = preset = running = flag = bad = 0;
        trace = pulse = restores = restore_busy = stop_busy = stop_delayed = 0;
        irq = 0x83;
        int contained = -1;
        int stuck = mode == BUSY_AFTER_START || mode == INVALID_AFTER_START;
        int incomplete = stuck || mode == BUSY_ON_RESTORE;
        if (opn_probe(irq, &contained) != expected[mode] || bad ||
            contained != !(incomplete || mode == NO_CLEAR) || running != (unsigned)stuck ||
            irq != (incomplete ? 0x80u : 0x83u) || restores != (incomplete ? 0 : starts))
            return mode + 1;
        if ((mode <= MOVING_STATUS && writes) || (mode == FLOATING && reads < 2) ||
            (mode == OPNA && (stops != 1 || starts)) ||
            ((mode == NO_RISE || mode == NO_CLEAR || mode == OPN || mode >= BUSY_AFTER_START) &&
             (stops != (stuck ? 1u : 2u) || starts != 1)))
            return mode + 1;
    }
    return 0;
}

void _start(void)
{
    int rc = cases();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
