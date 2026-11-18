#include <stdbool.h>
#include "via.h"

enum {
    PORT_B = 0x0000, DDR_B = 0x0400, SR = 0x1400, ACR = 0x1600,
    IFR = 0x1a00, IER = 0x1c00,
    PB3 = 0x08, PB4 = 0x10, TIP = 0x20,
    SHIFT_MODE = 0x0c, SHIFT_OUT = 0x10, SR_DONE = 0x04, PMU_IRQ = 0x10,
    CUDA_LINES = PB4 | TIP, CUDA_FRAME_MAX = 256,
    PMU_RESET_CPU = 0xd0, CUDA_PSEUDO = 0x01, CUDA_RESET = 0x11,
    PMU_READ_RTC = 0x38, CUDA_GET_TIME = 0x03,
    RTC_BYTES = 4, CUDA_REPLY_HEADER = 3,
    /* Delay units are 50 us. */
    DELAY_UNITS_PER_SECOND = 20000, CUDA_SETTLE = 3, CUDA_RETRY = 30,
    EDGE_INTERVALS_PER_SECOND = 10
};

enum {
    VIA_INVALID = -1, PMU_IDLE_TIMEOUT = -2, PMU_BUSY_TIMEOUT = -3,
    PMU_FREE_TIMEOUT = -4, CUDA_SEND_TIMEOUT = -5, CUDA_RECEIVE_TIMEOUT = -6,
    CUDA_FRAME_TOO_LONG = -7
};

typedef unsigned long long via_ticks;

static u8 read8(struct via_controller *v, u32 reg)
{
    return v->io.read8(v->io.cookie, reg);
}

static void write8(struct via_controller *v, u32 reg, u8 value)
{
    v->io.write8(v->io.cookie, reg, value);
}

static void update(struct via_controller *v, u32 reg, u8 clear, u8 set)
{
    write8(v, reg, (read8(v, reg) & ~clear) | set);
}

static via_ticks now(struct via_controller *v)
{
    return v->io.tb(v->io.cookie);
}

static bool expired(struct via_controller *v, via_ticks start, via_ticks current)
{
    return current - start >= v->io.tb_hz;
}

static bool delay(struct via_controller *v, via_ticks start, u32 units)
{
    const u32 hz = v->io.tb_hz, scale = DELAY_UNITS_PER_SECOND;
    /* Divide before multiplying to keep the exact ceiling in 32 bits. */
    u32 ticks = (hz / scale) * units + ((hz % scale) * units + scale - 1) / scale;
    via_ticks begin = now(v);
    for (;;) {
        via_ticks current = now(v);
        if (expired(v, start, current))
            return false;
        if (current - begin >= ticks)
            return true;
    }
}

static bool wait_bit(struct via_controller *v, via_ticks start, u32 reg, u8 bit,
                     bool high)
{
    const u32 scale = EDGE_INTERVALS_PER_SECOND, hz = v->io.tb_hz;
    u32 ticks = hz / scale + (hz % scale != 0);
    via_ticks begin = now(v);
    for (via_ticks current = begin;; current = now(v)) {
        if (expired(v, start, current) || current - begin >= ticks)
            return false;
        if (((read8(v, reg) & bit) != 0) == high)
            return true;
    }
}

static bool cuda_pending(struct via_controller *v)
{
    return (read8(v, PORT_B) & PB3) == 0;
}

static void cuda_toggle_ack(struct via_controller *v)
{
    write8(v, PORT_B, read8(v, PORT_B) ^ PB4);
}

static void cuda_idle(struct via_controller *v)
{
    update(v, ACR, SHIFT_OUT, 0);
    update(v, PORT_B, 0, CUDA_LINES);
}

int via_prepare(struct via_controller *v)
{
    if (!v->io.read8 || !v->io.write8 || !v->io.tb || !v->io.tb_hz)
        return VIA_INVALID;
    if (v->kind == VIA_PMU) {
        update(v, PORT_B, 0, PB4);
        update(v, DDR_B, PB3, PB4);
        return 0;
    }
    if (v->kind != VIA_CUDA)
        return VIA_INVALID;

    update(v, DDR_B, 0, CUDA_LINES);
    update(v, DDR_B, PB3, 0);
    update(v, ACR, SHIFT_OUT, SHIFT_MODE);
    update(v, PORT_B, 0, CUDA_LINES);
    (void)read8(v, SR);
    write8(v, IER, SR_DONE);
    update(v, PORT_B, 0, CUDA_LINES);
    via_ticks start = now(v);
    if (!delay(v, start, CUDA_SETTLE))
        goto timeout;
    update(v, PORT_B, TIP, 0);
    if (!delay(v, start, CUDA_SETTLE))
        goto timeout;
    cuda_toggle_ack(v);
    if (!delay(v, start, CUDA_SETTLE))
        goto timeout;
    update(v, PORT_B, 0, TIP);
    if (!delay(v, start, CUDA_SETTLE))
        goto timeout;
    update(v, PORT_B, 0, CUDA_LINES);
    (void)read8(v, SR);
    return 0;

timeout:
    cuda_idle(v);
    return CUDA_SEND_TIMEOUT;
}

static int pmu_transfer(struct via_controller *v, via_ticks start, u8 cmd, u8 *reply)
{
    int rc = PMU_IDLE_TIMEOUT;
    if (wait_bit(v, start, PORT_B, PB3, true)) {
        if (reply) {
            update(v, ACR, SHIFT_OUT, SHIFT_MODE);
            (void)read8(v, SR);
        } else {
            update(v, ACR, 0, SHIFT_MODE | SHIFT_OUT);
            write8(v, SR, cmd);
        }
        update(v, PORT_B, PB4, 0);
        rc = PMU_BUSY_TIMEOUT;
        if (wait_bit(v, start, PORT_B, PB3, false)) {
            update(v, PORT_B, 0, PB4);
            if (!wait_bit(v, start, PORT_B, PB3, true))
                return PMU_FREE_TIMEOUT;
            if (reply)
                *reply = read8(v, SR);
            return 0;
        }
    }
    update(v, PORT_B, 0, PB4);
    return rc;
}

/* Both PMU commands have zero request payload. Read RTC has a fixed
 * four-byte response, with no length or command byte on the wire. */
static int pmu_command(struct via_controller *v, via_ticks start, u8 cmd,
                       u8 *in, u32 cap, u32 *got)
{
    u32 reply = cmd == PMU_RESET_CPU ? 0 : RTC_BYTES;
    if (reply && (!in || cap < reply))
        return VIA_INVALID;

    u8 enabled = read8(v, IER) & PMU_IRQ;
    write8(v, IER, enabled);
    int rc = pmu_transfer(v, start, cmd, NULL);
    while (!rc && *got < reply) {
        rc = pmu_transfer(v, start, 0, in + *got);
        if (!rc)
            ++*got;
    }
    if (reply && enabled)
        write8(v, IER, 0x80 | enabled);
    return rc;
}

static int cuda_receive(struct via_controller *v, via_ticks start,
                        u8 *in, u32 cap, u32 *got)
{
    int rc = CUDA_RECEIVE_TIMEOUT;
    if (delay(v, start, CUDA_SETTLE)) {
        (void)read8(v, SR); /* Dummy byte at start of frame. */
        if (!cuda_pending(v))
            return 1; /* Fake start: no frame was received. */
        update(v, ACR, SHIFT_OUT, 0);
        update(v, PORT_B, TIP, 0);
        for (u32 n = 0; n < CUDA_FRAME_MAX; ++n) {
            if (!wait_bit(v, start, IFR, SR_DONE, true))
                break;
            u8 byte = read8(v, SR);
            if (*got < cap)
                in[(*got)++] = byte;
            if (!cuda_pending(v)) {
                rc = 0;
                break;
            }
            if (n + 1 == CUDA_FRAME_MAX) {
                rc = CUDA_FRAME_TOO_LONG;
                break;
            }
            cuda_toggle_ack(v);
        }
    }
    cuda_idle(v);
    return rc;
}

static int cuda_send(struct via_controller *v, via_ticks start, const u8 *out, u32 n)
{
    int rc = CUDA_SEND_TIMEOUT;
    u32 discarded = 0;
    for (;;) {
        if (!delay(v, start, CUDA_SETTLE))
            return CUDA_SEND_TIMEOUT;
        if (!cuda_pending(v)) {
            (void)read8(v, SR); /* Clear a stale completion before shifting. */
            update(v, ACR, 0, SHIFT_OUT);
            write8(v, SR, out[0]);
            update(v, PORT_B, TIP, PB4);
            rc = CUDA_SEND_TIMEOUT;
            for (u32 i = 1; i <= n; ++i) {
                if (!wait_bit(v, start, IFR, SR_DONE, true))
                    break;
                (void)read8(v, SR);
                if (cuda_pending(v)) {
                    rc = 1; /* Collision, go back to input before receiving. */
                    break;
                }
                if (i == n) {
                    rc = 0;
                    break;
                }
                write8(v, SR, out[i]);
                cuda_toggle_ack(v);
            }
            cuda_idle(v);
            if (rc <= 0)
                return rc;
        }
        rc = cuda_receive(v, start, NULL, 0, &discarded);
        if (rc < 0)
            return rc;
        if (!delay(v, start, CUDA_RETRY))
            return CUDA_SEND_TIMEOUT;
    }
}

static int via_command(struct via_controller *v, const u8 *out, u32 n,
                       u8 *in, u32 cap, u32 *got)
{
    *got = 0;
    if (!n || (v->kind != VIA_CUDA && (v->kind != VIA_PMU || n != 1 ||
                                       (out[0] != PMU_RESET_CPU && out[0] != PMU_READ_RTC))))
        return VIA_INVALID;
    via_ticks start = now(v);
    if (v->kind == VIA_PMU)
        return pmu_command(v, start, out[0], in, cap, got);

    int rc = cuda_send(v, start, out, n);
    if (rc || !in || !cap)
        return rc;
    do {
        if (!wait_bit(v, start, PORT_B, PB3, false))
            return CUDA_RECEIVE_TIMEOUT;
        rc = cuda_receive(v, start, in, cap, got);
    } while (rc == 1);
    return rc;
}

int via_restart(struct via_controller *v)
{
    static const u8 pmu[] = { PMU_RESET_CPU }, cuda[] = { CUDA_PSEUDO, CUDA_RESET };
    u32 got;
    if (v->kind == VIA_PMU)
        return via_command(v, pmu, sizeof(pmu), NULL, 0, &got);
    if (v->kind == VIA_CUDA)
        return via_command(v, cuda, sizeof(cuda), NULL, 0, &got);
    return VIA_INVALID;
}

int via_read_rtc(struct via_controller *v, u32 *seconds)
{
    static const u8 pmu[] = { PMU_READ_RTC };
    static const u8 cuda[] = { CUDA_PSEUDO, CUDA_GET_TIME };
    /* One extra byte lets a truncated oversized Cuda frame fail validation. */
    u8 reply[CUDA_REPLY_HEADER + RTC_BYTES + 1];
    u32 got, offset;
    int rc;

    if (v->kind == VIA_PMU) {
        rc = via_command(v, pmu, sizeof(pmu), reply, sizeof(reply), &got);
        offset = 0;
    } else if (v->kind == VIA_CUDA) {
        rc = via_command(v, cuda, sizeof(cuda), reply, sizeof(reply), &got);
        offset = CUDA_REPLY_HEADER;
    } else {
        return VIA_INVALID;
    }
    if (rc)
        return rc;
    if (got != offset + RTC_BYTES ||
        (offset && (reply[0] != CUDA_PSEUDO || reply[1] != 0 || reply[2] != CUDA_GET_TIME)))
        return VIA_INVALID;

    *seconds = 0;
    for (u32 i = 0; i < RTC_BYTES; ++i)
        *seconds = (*seconds << 8) | reply[offset + i];
    return 0;
}
