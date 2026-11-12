/* Real transport/client/protocol, with only TB, LAN and privilege observation fake. */
#include "platform/powermac/hostfs.h"
#include "os32_kapi_shared.h"
#include "arch/ppc/ppc_trap.h"
#include "exec_arch.h"

volatile u32 kctx_irq_depth;
static u64 tb;
static u32 polls, sends, mark, busy_left;
static int state, gmac, send_rc, drop_state, reply_mode;
static u8 reply[PLN1_FS_FRAME_MAX];
static u32 reply_len;
static void *ctx = &pm_hostfs_transport;

static void output(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void finish(u32 rc) __attribute__((noreturn));
static void finish(u32 rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) { output("FAIL\n", 5); finish(mark); } } while (0)
static void checkpoint(u32 n)
{
    mark = n;
    output((const char *)&mark, sizeof(mark));
}
u64 pm_hfs_tb(void) { return tb; }
int pm_lan_fs_running(void) { return gmac && state == 5; }
void pm_lan_poll(u64 now)
{
    CHECK(now == tb);
    ++polls;
    tb += pm_hostfs_transport.tb_hz / 100u;
    if (drop_state) state = 6;
    if (reply_len) {
        CHECK(pm_hfs_rx_frame(reply, reply_len) == 0);
        reply_len = 0;
    }
    /* A stopped clock is detected by an explicit reached runtime verdict,
     * rather than treating a subprocess watchdog timeout as RED. */
    if (polls > 150) { output("STOPPED-CLOCK\n", 14); finish(41); }
}
int pm_lan_fs_send(const u8 *frame, u32 len)
{
    ++sends;
    CHECK(frame != 0 && len != 0);
    if (busy_left) { --busy_left; return LAN_EBUSY; }
    if (reply_mode) {
        u8 body[SFS_HELLO_RSP_LEN] = {0};
        CHECK(frame[SFS_OFF_TYPE] == SFS_T_HELLO);
        sfs_put16(body + SFS_HELLO_RSP_VER, SFS_VERSION);
        sfs_put16(body + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
        sfs_put32(body + SFS_HELLO_RSP_NONCE,
                  sfs_get32(frame + SFS_HDR_LEN + SFS_HELLO_REQ_NONCE));
        reply_len = sfs_encode(reply, SFS_T_HELLO | SFS_T_RESP, 7, 0,
                               body, sizeof(body));
    }
    return send_rc;
}

/* Real dispatcher proves both interrupt branches wrap the actual callback,
 * including nested depth=2 and depth restoration. USER SC remains waitable. */
u32 ppc_trap_msr(void) { return 0x30; }
u32 ppc_root_current(void) { return 0; }
void ppc_trap_stop(void) { finish(90); }
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 r) { (void)f; (void)r; finish(91); }
void ring3_fault_kill(void) { finish(92); }
void exec_syscall_dispatch(struct arch_syscall *call)
{
    (void)call;
    CHECK(pm_hostfs_io.can_wait(ctx));
}
static u32 expected_depth;
static void interrupt(struct ppc_frame *f)
{
    u32 before = polls;
    (void)f;
    CHECK(kctx_irq_depth == expected_depth);
    pm_hostfs_io.idle(ctx);
    CHECK(polls == before);
    CHECK(!pm_hostfs_io.can_wait(ctx));
    CHECK(pm_hostfs_io.put_until(ctx, reply, 16, 100) < 0);
    CHECK(sends == 0);
}
static void reset(void)
{
    tb = 0; polls = sends = busy_left = 0;
    state = 5; gmac = 1; send_rc = LAN_OK;
    drop_state = reply_mode = 0; reply_len = 0;
    pm_hostfs_transport.rx_pos = pm_hostfs_transport.rx_end = 0;
    kctx_irq_depth = 0;
    CHECK(pm_hfs_set_clock(1000000) == 0);
}
static void receive_tests(void)
{
    u8 body[SFS_LAN_MAX_PAYLOAD], frame[PLN1_FS_FRAME_MAX + 1];
    u32 i, n;
    checkpoint(20);
    for (i = 0; i < sizeof(body); ++i) body[i] = (u8)i;
    n = sfs_encode_limit(frame, SFS_T_READ | SFS_T_RESP, 7, 3,
                         body, sizeof(body), SFS_LAN_MAX_PAYLOAD);
    CHECK(n == 1044);
    CHECK(pm_hfs_rx_frame(frame, n) == 0);
    CHECK(pm_hostfs_io.get(ctx) == frame[0]);
    CHECK(pm_hfs_rx_frame(frame, n) < 0); /* unread suffix stays intact */
    for (i = 1; i < n; ++i) CHECK(pm_hostfs_io.get(ctx) == frame[i]);
    CHECK(pm_hostfs_io.get(ctx) == -1);
    CHECK(pm_hfs_rx_frame(frame, n - 1) < 0);
    CHECK(pm_hfs_rx_frame(frame, n + 1) < 0);
    frame[100] ^= 1;
    CHECK(pm_hfs_rx_frame(frame, n) < 0);
    frame[100] ^= 1;
    CHECK(pm_hfs_rx_frame(frame + 1, n - 1) < 0);
    CHECK(pm_hfs_rx_frame(frame, 2) < 0);
    /* A truncated datagram contributes no prefix to the next one. */
    CHECK(pm_hfs_rx_frame(frame, 40) < 0);
    n = sfs_encode(frame, SFS_T_STAT | SFS_T_RESP, 8, 4, body, 4);
    CHECK(pm_hfs_rx_frame(frame, n) == 0);
    for (i = 0; i < n; ++i) CHECK(pm_hostfs_io.get(ctx) == frame[i]);
    CHECK(pm_hostfs_io.get(ctx) == -1);
    /* Even a valid short frame must not expose Ethernet padding. */
    frame[n] = 0;
    CHECK(pm_hfs_rx_frame(frame, n + 1) < 0);
}
static int exercise(void)
{
    u8 frame[16] = {0};
    struct ppc_frame f = {.vector = 0xc00, .srr1 = PPC_MSR_PR};
    SfsClient client;
    reset();
    checkpoint(10);
    CHECK(pm_hfs_set_clock(999999) < 0);
    CHECK(pm_hfs_set_clock(1000000001) < 0);
    CHECK(pm_hfs_set_clock(1000000000) == 0);
    reset();
    checkpoint(40);
    busy_left = 10000;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 4) == OS32_ERR_TIMEOUT);
    CHECK(polls == 4 && sends == 4);
    CHECK(pm_hostfs_io.now(ctx) == 4);
    CHECK(pm_hfs_set_clock(1000000000) == 0);
    tb = (u64)10000000 * 0x100000002ULL;
    CHECK(pm_hostfs_io.now(ctx) == 2);
    reset();
    checkpoint(11);
    kctx_irq_depth = 1;
    pm_hostfs_io.idle(ctx);
    CHECK(polls == 0);
    CHECK(!pm_hostfs_io.can_wait(ctx));
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 100) < 0);
    CHECK(sends == 0 && polls == 0);
    for (u32 d = 0; d < 2; ++d) {
        kctx_irq_depth = d; expected_depth = d + 1;
        for (u32 v = 0; v < 2; ++v) {
            f.vector = v ? PPC_VECTOR_DEC : 0x500;
            ppc_trap_set_handler(f.vector, interrupt);
            ppc_trap_dispatch(&f);
            CHECK(kctx_irq_depth == d);
        }
    }
    kctx_irq_depth = 0; f.vector = 0xc00;
    ppc_trap_dispatch(&f);
    reset();
    checkpoint(12);
    for (state = 0; state <= 6; ++state) {
        if (state == 5) continue;
        CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 100) < 0);
        CHECK(sends == 0 && polls == 0);
    }
    state = 5; gmac = 0;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 100) < 0);
    CHECK(sends == 0 && polls == 0);
    reset(); receive_tests();
    checkpoint(30);
    busy_left = 2;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 10) == 0);
    CHECK(sends == 3 && polls == 2);
    reset();
    checkpoint(31);
    busy_left = 5; drop_state = 1;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 10) < 0);
    CHECK(sends == 1 && polls == 1);
    reset();
    checkpoint(32);
    for (int rc = LAN_EINVAL; rc >= LAN_EUNSUPPORTED; --rc) {
        send_rc = rc;
        CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 10) < 0);
        CHECK(polls == 0);
    }
    send_rc = LAN_AGAIN;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 10) < 0);
    CHECK(polls == 0);
    reset();
    checkpoint(42);
    tb = (u64)0xfffffffeu * 10000u; busy_left = 10000;
    CHECK(pm_hostfs_io.put_until(ctx, frame, sizeof(frame), 1) == OS32_ERR_TIMEOUT);
    CHECK(polls == 3);
    reset();
    checkpoint(43);
    busy_left = 10000;
    CHECK(pm_hostfs_io.put(ctx, frame, sizeof(frame)) == -1);
    CHECK(polls == sfs_ms_ticks(1000));
    reset();
    checkpoint(50);
    reply_mode = 1;
    sfs_client_init(&client, &pm_hostfs_io, ctx, 115200);
    client.max_payload = SFS_LAN_MAX_PAYLOAD;
    CHECK(sfs_client_hello_deadline(&client, 0x12345678, 100) == 0);
    CHECK(client.sid == 7 && client.max_payload == SFS_LAN_MAX_PAYLOAD);
    CHECK(sends == 1 && polls == 1 && client.resends == 0);
    output("PASS\n", 5);
    return 0;
}
void _start(void) { finish(exercise()); }
