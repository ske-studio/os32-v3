#include "kprintf.h"
#include "sys/fs/serialfs/serialfs.h"

static unsigned opens, closes, bye_writes, hello_puts, hello_writes, clock_now;
static int invalid_close;
static u8 reply[64];
static u32 reply_len;

static int probe_open(void)
{
    ++opens;
    return 42;
}

static int probe_write(const u8 *buf, u32 n)
{
    reply_len = 1; /* Nonempty status permits stopping before READ. */
    if (n < SFS_HDR_LEN) return n;
    if (buf[SFS_OFF_TYPE] == SFS_T_BYE) ++bye_writes;
    if (buf[SFS_OFF_TYPE] == SFS_T_HELLO) {
        u8 body[SFS_HELLO_RSP_LEN] = {0};
        u32 nonce = sfs_get32(buf + SFS_HDR_LEN + SFS_HELLO_REQ_NONCE);
        sfs_put16(body + SFS_HELLO_RSP_VER, SFS_VERSION);
        sfs_put16(body + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
        sfs_put32(body + SFS_HELLO_RSP_NONCE, nonce);
        ++hello_writes;
        reply_len = sfs_encode(reply, SFS_T_HELLO | SFS_T_RESP, hello_writes, 0,
                               body, sizeof(body));
        char hex[2 * (SFS_HDR_LEN + SFS_HELLO_REQ_LEN + SFS_CRC_LEN) + 1];
        static const char digits[] = "0123456789abcdef";
        if (2 * n >= sizeof(hex)) return -1;
        for (u32 i = 0; i < n; ++i) {
            hex[2 * i] = digits[buf[i] >> 4];
            hex[2 * i + 1] = digits[buf[i] & 15];
        }
        hex[2 * n] = 0;
        kprintf(7, "HFS hello=%s\r\n", hex);
    } else if (n <= sizeof(reply)) {
        for (u32 i = 0; i < n; ++i) reply[i] = buf[i];
        reply_len = n;
    }
    return n;
}

static int probe_close(int h, int owner)
{
    ++closes;
    if (h != 42 || owner) invalid_close = 1;
    return 0;
}

static void probe_cancel(const u8 *frame, u32 n, u32 deadline)
{
    if (pm_hostfs_io.cancel)
        pm_hostfs_io.cancel(&pm_hostfs_transport, frame, n, deadline);
}

const char *hfs_cancel_probe(void)
{
    /* The table mutation still builds with -Werror=unused-function. */
    (void)pm_hfs_cancel;
    void *ctx = &pm_hostfs_transport;
    for (unsigned steps = 0; steps <= 3; ++steps) {
        u8 frame[] = {1, (u8)steps}, copy[] = {1, (u8)steps}, other[] = {2, (u8)steps};
        unsigned before_open = opens, before_close = closes;
        if (pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 100) ||
            pm_hostfs_io.put_until(ctx, copy, sizeof(copy), 101))
            return "admission";
        for (unsigned i = 0; i < steps; ++i) pm_hfs_link_poll();
        probe_cancel(other, sizeof(other), 100);
        probe_cancel(copy, 1, 100);
        probe_cancel(copy, sizeof(copy), 101);
        if (!pending || until != 100 || phase != (int)steps || closes != before_close)
            return "unrelated cancel";
        probe_cancel(copy, sizeof(copy), 100);
        if (pending) return "hostfs cancel left pending";
        probe_cancel(copy, sizeof(copy), 100);
        pm_hfs_link_poll();
        if (handle != -1 || result != OS32_ERR_TIMEOUT || invalid_close ||
            opens != before_open + !!steps || closes != before_close + !!steps ||
            pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 101) != OS32_ERR_TIMEOUT ||
            pending) return "completed identity";
    }
    for (unsigned prefix = 0; prefix <= 1; ++prefix) {
        u8 expired[64], next[64];
        u32 len = sfs_encode(expired, SFS_T_STAT | SFS_T_RESP, 1, 2 * prefix + 1, 0, 0);
        u32 next_len = sfs_encode(next, SFS_T_STAT | SFS_T_RESP, 1, 2 * prefix + 2, 0, 0);
        unsigned before = closes;
        if (pm_hostfs_io.put_until(ctx, expired, len, 100)) return "poll admission";
        for (int i = 0; i < 4; ++i) pm_hfs_link_poll();
        probe_cancel(next, next_len, 100);
        probe_cancel(expired, len, 101);
        if (pm_hostfs_transport.rx_end - pm_hostfs_transport.rx_pos != len)
            return "unrelated cancel discarded reply";
        if (prefix && pm_hostfs_io.get(ctx) != expired[0]) return "partial read";
        clock_now = 100;
        probe_cancel(expired, len, 100);
        if (pending || result || closes != before + 1 || pm_hostfs_io.get(ctx) != -1 ||
            pm_hostfs_io.put_until(ctx, expired, len, 101)) return "expired reply";
        if (pm_hostfs_io.put_until(ctx, next, next_len, 101)) return "next admission";
        for (int i = 0; i < 4; ++i) pm_hfs_link_poll();
        probe_cancel(expired, len, 100);
        for (u32 i = 0; i < next_len; ++i)
            if (pm_hostfs_io.get(ctx) != next[i]) return "next reply";
        if (pm_hostfs_io.get(ctx) != -1) return "trailing reply";
        kprintf(7, "HFS discarded prefix=%u\r\n", prefix);
        clock_now = 0;
    }

    SfsClient *client = (SfsClient *)sfs_persistent_client();
    if (!client || sfs_ensure_session(100)) return "initial HELLO";
    unsigned first_sid = client->sid;
    client->seq = 0xf001;
    if (!first_sid || sfs_ensure_session(100) || !client->sid || client->sid == first_sid ||
        bye_writes || hello_puts != 2 || hello_writes != 2) return "seq renewal";
    return "pass";
}
