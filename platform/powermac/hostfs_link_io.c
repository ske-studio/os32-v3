/* LAN_OPERATOR_DESIGN §5: one deferred SFS CALL, one absolute deadline.
 * SfsClient's retries must never allocate a second link request for this frame.
 * PLN1 keeps its independent transport in hostfs_io.c. */
#include "hostfs.h"
#include "hostfs_trace.h"
#include "fs/serialfs/sfs_lan.h"
#include "platform_host.h"
#include "net/link_platform.h"
#include "kernel/idle_work.h"
#include "kernel/irq/irq.h"
#include "arch_io.h"
#include "os32_kapi_shared.h"

struct pm_hfs_transport pm_hostfs_transport;
static u8 tx[SFS_BUFFER_FRAME], response[SFS_BUFFER_FRAME];
static u32 tx_len, written, received, length, until;
static i32 handle = -1;
static int pending, result;

enum { HFS_OPEN, HFS_WRITE, HFS_STATUS, HFS_READ };
static int phase;

int pm_hfs_set_clock(u32 tb_hz)
{
    if (tb_hz < 1000000u || tb_hz > 1000000000u) return -1;
    pm_hostfs_transport.tb_hz = tb_hz;
    return 0;
}
static u32 pm_hfs_now(void *ctx)
{
    (void)ctx;
    return link_platform_now();
}
static int pm_hfs_can_wait(void *ctx)
{
    (void)ctx;
    return kctx_irq_depth == 0 && _irq_enabled();
}

static const char *accept_frame(const u8 *frame, u32 len)
{
    struct pm_hfs_transport *t = &pm_hostfs_transport;
    u32 i, payload;
    if (t->rx_pos != t->rx_end) return "rx_busy";
    if (len < SFS_HDR_LEN + SFS_CRC_LEN || len > sizeof(t->rx)) return "frame_length";
    if (frame[0] != SFS_ENQ || frame[SFS_OFF_MAGIC1] != SFS_MAGIC1 ||
        frame[SFS_OFF_MAGIC2] != SFS_MAGIC2) return "preamble";
    payload = sfs_get16(frame + SFS_OFF_LEN);
    if (payload > SFS_LAN_MAX_PAYLOAD ||
        len != SFS_HDR_LEN + payload + SFS_CRC_LEN) return "frame_length";
    if (sfs_crc32(frame + SFS_OFF_TYPE, len - SFS_PREAMBLE_LEN - SFS_CRC_LEN) !=
        sfs_get32(frame + len - SFS_CRC_LEN)) return "crc";
    for (i = 0; i < len; ++i) t->rx[i] = frame[i];
    t->rx_pos = 0;
    t->rx_end = len;
    return 0;
}

int pm_hfs_rx_frame(const u8 *frame, u32 len)
{
    return accept_frame(frame, len) ? -1 : 0;
}

static int pm_hfs_get(void *ctx)
{
    struct pm_hfs_transport *t = ctx;
    return t->rx_pos < t->rx_end ? t->rx[t->rx_pos++] : -1;
}

static void finish(int rc, const char *reason, i32 detail)
{
    static const char *const names[] = { "OPEN", "WRITE", "STATUS", "READ" };
    pm_hfs_trace_finish(handle, names[phase], reason, rc, detail, received, length);
    if (handle >= 0) (void)platform_host_close(handle, 0);
    handle = -1;
    pending = 0;
    result = rc;
}

/* Registered alongside the link foreground service, also used by SfsIo.idle. */
void pm_hfs_link_poll(void)
{
    i32 rc;
    if (!pending || !pm_hfs_can_wait(0)) return;
    if ((i32)(pm_hfs_now(0) - until) >= 0) {
        finish(OS32_ERR_TIMEOUT, "timeout", 0);
        return;
    }
    switch (phase) {
    case HFS_OPEN: {
        char request[24] = "SFS CALL ";
        char digits[10];
        u32 n = tx_len, count = 0, pos = 9;
        do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
        while (count) request[pos++] = digits[--count];
        rc = platform_host_open(request, pos, 0);
        /* No WDATA has been sent: retry admission within the same deadline. */
        if (rc == OS32_ERR_AGAIN || rc == OS32_ERR_HOSTDOWN ||
            rc == OS32_ERR_FULL) return;
        if (rc < 0) { finish(rc, "open_rc", rc); return; }
        handle = rc;
        phase = HFS_WRITE;
        return;
    }
    case HFS_WRITE:
        rc = platform_host_write(handle, tx + written, tx_len - written, 0);
        if (rc == OS32_ERR_AGAIN) return;
        if (rc <= 0 || (u32)rc > tx_len - written) {
            finish(-1, "write_rc", rc);
            return;
        }
        written += (u32)rc;
        if (written == tx_len) phase = HFS_STATUS;
        return;
    case HFS_STATUS: {
        u32 status;
        rc = platform_host_status(handle, &status, &length, 0);
        if (rc == OS32_ERR_AGAIN) return;
        if (rc) { finish(-1, "status_rc", rc); return; }
        if (status != 200) { finish(-1, "http_status", status); return; }
        if (length > sizeof(response)) { finish(-1, "response_length", length); return; }
        /* BYE and other one-way frames complete with an empty 200. */
        if (!length) { finish(0, "empty", 0); return; }
        phase = HFS_READ;
        return;
    }
    case HFS_READ: {
        const u8 *data;
        rc = platform_host_read_stage(handle, length - received, &data, 0);
        if (rc == OS32_ERR_AGAIN) return;
        if (rc <= 0 || (u32)rc > length - received) {
            finish(-1, "read_rc", rc);
            return;
        }
        for (i32 i = 0; i < rc; ++i) response[received++] = data[i];
        if (received == length) {
            const char *error = accept_frame(response, length);
            finish(error ? -1 : 0, error ? error : "ok", 0);
        }
        return;
    }
    }
}

static void pm_hfs_idle(void *ctx)
{
    /* FG1 wait: progress work, keeping RX active for NET_RX_ACTIVE_MS.
     * Otherwise rest until an interrupt. IRQ context and EE=0 neither
     * poll nor sleep (pm_hfs_can_wait). */
    if (!pm_hfs_can_wait(ctx)) return;
    idle_wait_step();
}
static int pm_hfs_put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    if (!pm_hfs_can_wait(ctx)) return -1;
    if ((i32)(pm_hfs_now(ctx) - deadline) >= 0) return OS32_ERR_TIMEOUT;
    if (!buf || !n || n > sizeof(tx)) return -1;
    int same = n == tx_len;
    for (u32 i = 0; same && i < n; ++i) if (buf[i] != tx[i]) same = 0;
    /* Preserve both completed and failed identities: no reopen after lost reply.
     * A different sid/seq (or HELLO nonce) starts a genuinely new SFS frame. */
    if (same) return result;
    if (pending) return -1;
    for (u32 i = 0; i < n; ++i) tx[i] = buf[i];
    tx_len = n;
    until = deadline;
    written = received = length = 0;
    phase = HFS_OPEN;
    pending = 1;
    result = 0;
    return 0;
}
static int pm_hfs_put(void *ctx, const u8 *buf, u32 n)
{
    u32 deadline = pm_hfs_now(ctx) + sfs_ms_ticks(SFS_LAN_PUT_MS);
    return pm_hfs_put_until(ctx, buf, n, deadline) == 0 ? 0 : -1;
}
const SfsIo pm_hostfs_io = {
    pm_hfs_put, pm_hfs_get, pm_hfs_now, pm_hfs_idle,
    pm_hfs_can_wait, pm_hfs_put_until
};
