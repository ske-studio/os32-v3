#include "fm.h"

/* Model only the two bank-zero ports. Unexpected accesses fail the case. */
enum scenario {
    FLOATING, BUSY, RESERVED, MOVING_STATUS, MOVING_ID, OPNA, OPN,
    NO_RISE, STALE_FLAG, NO_CLEAR, BUSY_AFTER_ID, CASE_COUNT
};

static enum scenario mode;
static unsigned reads, latch, ids, writes, stops, starts, preset, running, flag, bad;

unsigned pcm_shim_inp(unsigned port)
{
    if (port == 0x18a) {
        if (latch != 0xff || reads < 2)
            bad = 1;
        ++ids;
        return mode == OPNA || (mode == MOVING_ID && ids == 1) ? 1 : 0xff;
    }
    if (port != 0x188 || ++reads > 50000)
        bad = 1;
    if (mode == FLOATING) return 0xff;
    if (mode == BUSY || (mode == BUSY_AFTER_ID && ids)) return 0x80;
    if (mode == RESERVED) return 0x40;
    if (mode == MOVING_STATUS) return reads & 1;
    if (mode == STALE_FLAG) return 2;
    if (mode == OPN && reads <= 5) return 0x80;
    if (running && mode != NO_RISE) flag = 2;
    return flag;
}

void pcm_shim_outp(unsigned port, unsigned value)
{
    ++writes;
    if (port == 0x188) {
        latch = value;
    } else if (port != 0x18a || ids < 2) {
        bad = 1;
    } else if (latch == 0x27 && value == 0x30) {
        ++stops;
        running = 0;
        if (mode != NO_CLEAR) flag = 0;
    } else if (latch == 0x26 && value == 0xff && stops) {
        preset = 1;
    } else if (latch == 0x27 && value == 0x0a && preset) {
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
        OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN, OPN_PROBE_UNKNOWN
    };
    for (mode = FLOATING; mode < CASE_COUNT; ++mode) {
        reads = latch = ids = writes = stops = starts = preset = running = flag = bad = 0;
        if (opn_probe() != expected[mode] || bad || running)
            return mode + 1;
        if ((mode <= MOVING_STATUS && writes) || (mode == FLOATING && reads < 2) ||
            (mode == OPNA && (stops != 1 || starts)) ||
            ((mode == NO_RISE || mode == NO_CLEAR || mode == OPN) &&
             (stops != 2 || starts != 1)))
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
