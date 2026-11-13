/* A2 NIC unit: real nic.c, fake adapter and A3/link boundaries.
 * No A3 filtering/queues or hardware behavior is claimed by this unit. */
#include "hostsvc_fixture.h"
#include "net/nic_rx.h"
#include "net/rx_work.h"
#include "kernel/irq/irq.h"
#include "net/link.h"
#include "kernel/irq/tick.h"
#include "platform/pc98/tick_order.h"
#include "os32_kapi_shared.h"
#include "drivers/net/e82557/e82557_regs.h"
#include "kprintf.h"
#include <stdarg.h>

#define REQUIRE(expr) do { if (!(expr)) return (__LINE__ % 250) + 1; } while (0)
static int acquired, released, selected, acquire_rc, invalid_ops;
static int polls, demuxes, links, inits, resets[3], bad_if, order, recurse;
static int tick_rc, registrations;
static int tick_logs, logged_tick_rc, poll_reentry, poll_reentry_rc;
static tick_fn callback;
static u8 seen_peer[6], seen_local[6];
static int saw_peer;
u32 link_hello_ok;
u8 link_peer_mac[ETH_ALEN];
static u32 pending, rx_calls, rx_head;
/* Match the 82557 hardware ring's capacity; A3 owns separate protocol queues. */
static u8 rx_frames[E82557_RFD_COUNT][ETH_FRAME_MAX - ETH_FCS_LEN];
static u32 rx_lengths[E82557_RFD_COUNT];
static u8 sent[(ETH_FRAME_MAX - ETH_FCS_LEN)];
static u32 sent_len;
static const u8 mac_addr[6] = {2, 3, 4, 5, 6, 7};
static const u8 peer_addr[6] = {8, 9, 10, 11, 12, 13};
static int equal(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}
unsigned int nic_test_irq_save(void)
{
    unsigned int saved = hs_if();
    hs_set_if(0);
    return saved;
}
void nic_test_irq_restore(unsigned int flags) { hs_set_if(flags); }
static void check_cli(void) { if (hs_if()) bad_if++; }
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    static const char expected[] = "[nic] tick registration failed: %d\n";
    va_list args;
    check_cli();
    if (attr != ATTR_YELLOW || !equal(fmt, expected, sizeof(expected))) bad_if++;
    va_start(args, fmt);
    logged_tick_rc = va_arg(args, int);
    va_end(args);
    tick_logs++;
}
int tick_register(unsigned int slot, tick_fn fn)
{
    registrations++;
    check_cli();
    if (tick_rc) return tick_rc;
    if (slot >= TICK_SLOT_COUNT || !fn) return TICK_ERR_INVAL;
    if (callback && callback != fn) return TICK_ERR_BUSY;
    if (slot != PC98_TICK_LINK) bad_if++;
    callback = fn;
    return 0;
}
static int tx(const void *frame, u32 len)
{
    int rc;
    check_cli();
    if (hs_failure_take("nic.tx", &rc)) return rc;
    if (len > sizeof(sent)) return OS32_ERR_INVAL;
    for (u32 i = 0; i < len; i++) sent[i] = ((const u8 *)frame)[i];
    sent_len = len;
    return 0;
}
static int rx(void *out, u32 cap, u32 *len)
{
    int rc;
    if (!hs_if() && !kctx_irq_depth) bad_if++;
    rx_calls++;
    if (hs_failure_take("nic.rx", &rc)) return rc;
    if (!pending) return OS32_ERR_AGAIN;
    *len = rx_lengths[rx_head];
    if (cap < *len) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < *len; i++) ((u8 *)out)[i] = rx_frames[rx_head][i];
    rx_head = (rx_head + 1) % E82557_RFD_COUNT;
    pending--;
    return 0;
}
static void mac(u8 *out)
{
    check_cli();
    for (u32 i = 0; i < 6; i++) out[i] = mac_addr[i];
}
static void poll(void) { check_cli(); polls++; order = 1; }
static u32 credit(void) { check_cli(); return 4096; }
static const struct NetNicOps ops = {tx, rx, mac, poll, credit};
static const struct NetNicOps broken_ops = {tx, rx, mac, 0, credit};
static int acquire(const struct NetNicOps **out)
{
    if (!hs_if()) bad_if++;
    acquired++;
    if (acquire_rc) return acquire_rc;
    *out = invalid_ops ? &broken_ops : &ops;
    return 0;
}
static int acquire_e(const struct NetNicOps **out) { selected = 1; return acquire(out); }
static int acquire_l(const struct NetNicOps **out) { selected = 2; return acquire(out); }
static void release(void)
{
    if (!hs_if()) bad_if++;
    released++;
    /* Publication must be gone before task-context release. */
    if (net_tx(mac_addr, 6) != OS32_ERR_HOSTDOWN) bad_if++;
    int before = polls;
    if (callback) callback();
    if (polls != before) bad_if++;
}
const struct NetAdapter hs_adapter = {acquire_e, release};
static const struct NetAdapter lgy_adapter = {acquire_l, release};
static const struct NetAdapter replacement = {acquire_e, release};
int hs_rx(enum NetNicId nic, const void *frame, u32 len)
{
    if ((int)nic != selected) return OS32_ERR_HOSTDOWN;
    if (len > sizeof(rx_frames[0])) return OS32_ERR_INVAL;
    if (pending == E82557_RFD_COUNT) return OS32_ERR_AGAIN;
    u32 tail = (rx_head + pending) % E82557_RFD_COUNT;
    for (u32 i = 0; i < len; i++) rx_frames[tail][i] = ((const u8 *)frame)[i];
    rx_lengths[tail] = len;
    pending++;
    return 0;
}
int hs_tx_take(void *out, u32 cap, u32 *len)
{
    if (!sent_len) return OS32_ERR_AGAIN;
    *len = sent_len;
    if (cap < sent_len) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < sent_len; i++) ((u8 *)out)[i] = sent[i];
    sent_len = 0;
    return 0;
}
void net_rx_reset(u16 protocol) { check_cli(); resets[protocol]++; }
u32 net_rx_demux_budget(const struct NetNicOps *nic, const u8 local[6],
                         const u8 peer[6], struct rx_work_budget *b)
{
    u8 buf[(ETH_FRAME_MAX - ETH_FCS_LEN)]; u32 len;
    u32 count = 0;
    if (!hs_if() && !kctx_irq_depth) bad_if++;
    demuxes++;
    if (poll_reentry == 2) {
        poll_reentry = 0;
        int before = demuxes;
        if (net_nic_poll_once() != 0 || demuxes != before) bad_if++;
    }
    order = 2;
    saw_peer = peer != 0;
    for (u32 i = 0; i < 6; i++) {
        seen_local[i] = local[i];
        if (peer) seen_peer[i] = peer[i];
    }
    while (rx_work_take(b)) {
        if (nic->rx(buf, sizeof(buf), &len)) break;
        count++;
        if (len != ETH_ZLEN || buf[ETH_ZLEN - 1] != ETH_ZLEN) bad_if++;
    }
    return count;
}
void link_init(const u8 local[6])
{
    /* A2 fake does not register a tick; production ownership changes in A8. */
    check_cli(); inits++;
    if (!equal(local, mac_addr, 6)) bad_if++;
}
void link_tick(void)
{
    check_cli(); links++;
    if (order != 1 && order != 2) bad_if++;
    order = 3;
    if (poll_reentry) {
        int old_polls = polls, old_demuxes = demuxes;
        poll_reentry_rc = net_nic_poll_once();
        if (polls != old_polls || demuxes != old_demuxes || hs_if()) bad_if++;
    }
    if (recurse) net_tick();
}
static int test(void)
{
    u8 frame[ETH_ZLEN], out[(ETH_FRAME_MAX - ETH_FCS_LEN)]; u32 len;
    hs_reset(0);
    REQUIRE(OS32_ERR_HOSTDOWN == -18 && OS32_ERR_TIMEOUT == -19);
    REQUIRE(net_start(NET_NIC_AUTO, 0) == OS32_ERR_NOSYS);
    REQUIRE(net_start(NET_NIC_E82557, 0) == OS32_ERR_NOSYS);
    REQUIRE(net_start(NET_NIC_LGY98, 0) == OS32_ERR_NOSYS);
    REQUIRE(net_start((enum NetNicId)99, 0) == OS32_ERR_INVAL);
    REQUIRE(net_tx(frame, sizeof(frame)) == OS32_ERR_HOSTDOWN);
    REQUIRE(net_mac(out) == OS32_ERR_HOSTDOWN && net_rx_credit_bytes() == 0);
    REQUIRE(net_nic_poll_once() == 0); net_tick();
    REQUIRE(!polls && !links && hs_if());
    REQUIRE(net_bind(0) == OS32_ERR_INVAL);
    REQUIRE(net_bind(&broken_ops) == OS32_ERR_INVAL);
    REQUIRE(net_adapter_register(NET_NIC_AUTO, &hs_adapter) == OS32_ERR_INVAL);
    REQUIRE(net_adapter_register((enum NetNicId)-1, &hs_adapter) == OS32_ERR_INVAL);
    REQUIRE(net_adapter_register(NET_NIC_LGY98, 0) == OS32_ERR_INVAL);
    REQUIRE(net_adapter_register(NET_NIC_LGY98, &lgy_adapter) == 0);
    const int tick_errors[] = {TICK_ERR_CTX, TICK_ERR_INVAL, TICK_ERR_BUSY};
    for (u32 i = 0; i < sizeof(tick_errors) / sizeof(tick_errors[0]); i++) {
        tick_rc = tick_errors[i];
        REQUIRE(net_start(NET_NIC_AUTO, 0) == OS32_ERR_BUSY);
        REQUIRE(released == (int)i + 1 && !inits && !callback && hs_if());
        REQUIRE(tick_logs == (int)i + 1 && logged_tick_rc == tick_rc);
    }
    REQUIRE(net_tx(frame, sizeof(frame)) == OS32_ERR_HOSTDOWN); tick_rc = 0;
    REQUIRE(net_start(NET_NIC_AUTO, 0) == 0 && selected == 2);
    REQUIRE(registrations == 4 && callback == net_jiffy); /* No link_init registration. */
    int saved_inits = inits, saved_acquired = acquired;
    REQUIRE(net_start(NET_NIC_AUTO, 0) == 0);
    REQUIRE(inits == saved_inits && acquired == saved_acquired);
    REQUIRE(net_start(NET_NIC_E82557, 0) == OS32_ERR_BUSY);
    REQUIRE(net_start(NET_NIC_AUTO, peer_addr) == 0);
    REQUIRE(inits == saved_inits && acquired == saved_acquired);
    REQUIRE(net_bind(&ops) == OS32_ERR_BUSY);
    REQUIRE(net_mac(out) == 0 && equal(out, mac_addr, 6));
    REQUIRE(net_rx_credit_bytes() == 4096 && net_mac(0) == OS32_ERR_INVAL);
    net_unbind(); REQUIRE(released == 4 && hs_if());
    net_unbind(); REQUIRE(released == 4);
    REQUIRE(net_adapter_register(NET_NIC_E82557, &hs_adapter) == 0);
    REQUIRE(net_adapter_register(NET_NIC_E82557, &hs_adapter) == 0);
    REQUIRE(net_adapter_register(NET_NIC_E82557, &replacement) == OS32_ERR_BUSY);
    acquire_rc = OS32_ERR_AGAIN;
    REQUIRE(net_start(NET_NIC_AUTO, 0) == OS32_ERR_AGAIN && selected == 1);
    REQUIRE(released == 4); acquire_rc = 0;
    invalid_ops = 1;
    REQUIRE(net_start(NET_NIC_AUTO, 0) == OS32_ERR_INVAL);
    REQUIRE(released == 5); invalid_ops = 0;
    REQUIRE(net_start(NET_NIC_AUTO, peer_addr) == 0 && selected == 1);
    saved_inits = inits; saved_acquired = acquired;
    REQUIRE(net_start(NET_NIC_AUTO, peer_addr) == 0);
    REQUIRE(inits == saved_inits && acquired == saved_acquired);
    u8 changed_peer[6] = {8, 9, 10, 11, 12, 14};
    REQUIRE(net_start(NET_NIC_AUTO, changed_peer) == OS32_ERR_BUSY);
    REQUIRE(net_start(NET_NIC_AUTO, 0) == OS32_ERR_BUSY);
    REQUIRE(net_adapter_register(NET_NIC_E82557, &replacement) == OS32_ERR_BUSY);
    for (u32 i = 0; i < sizeof(frame); i++) frame[i] = (u8)(i + 1);
    hs_fail("nic.tx", OS32_ERR_AGAIN);
    REQUIRE(net_tx(frame, sizeof(frame)) == OS32_ERR_AGAIN);
    REQUIRE(hs_tx_take(out, sizeof(out), &len) == OS32_ERR_AGAIN);
    REQUIRE(net_tx(frame, sizeof(frame)) == 0 && hs_if());
    REQUIRE(hs_tx_take(out, sizeof(out), &len) == 0);
    REQUIRE(len == sizeof(frame) && equal(frame, out, len));
    hs_fail("nic.tx", OS32_ERR_HOSTDOWN);
    REQUIRE(net_tx(frame, sizeof(frame)) == OS32_ERR_HOSTDOWN);
    for (u32 i = 0; i < NET_RX_SCAN_MAX; i++)
        REQUIRE(hs_rx(NET_NIC_E82557, frame, sizeof(frame)) == 0);
    REQUIRE(hs_rx(NET_NIC_E82557, frame, sizeof(frame)) == OS32_ERR_AGAIN);
    int old_links = links, old_polls = polls;
    REQUIRE(net_nic_poll_once() == (int)NET_RX_SCAN_MAX && pending == 0);
    REQUIRE(polls == old_polls && links == old_links);
    REQUIRE(saw_peer && equal(seen_peer, peer_addr, 6));
    REQUIRE(equal(seen_local, mac_addr, 6) && hs_if());
    REQUIRE(hs_rx(NET_NIC_E82557, frame, sizeof(frame)) == 0);
    hs_fail("nic.rx", OS32_ERR_IO);
    REQUIRE(net_nic_poll_once() == 0 && pending == 1);
    REQUIRE(net_nic_poll_once() == 1 && !pending);
    hs_set_if(0);
    REQUIRE(net_tx(frame, sizeof(frame)) == 0 && !hs_if());
    REQUIRE(net_mac(out) == 0 && !hs_if());
    REQUIRE(net_rx_credit_bytes() == 4096 && !hs_if());
    REQUIRE(net_nic_poll_once() == 0 && !hs_if()); hs_set_if(1);
    poll_reentry = 2; net_rx_service();
    old_links = links; old_polls = polls; recurse = 1;
    poll_reentry = 1; poll_reentry_rc = -1;
    callback(); REQUIRE(links == old_links + 1 && polls == old_polls + 1);
    REQUIRE(poll_reentry_rc == 0 && !bad_if && hs_if());
    recurse = 0; poll_reentry = 0;
    int sfs = resets[NET_RX_SFS], link = resets[NET_RX_LINK];
    net_unbind();
    REQUIRE(resets[NET_RX_SFS] == sfs + 1 && resets[NET_RX_LINK] == link + 1);
    old_polls = polls; old_links = links;
    callback(); REQUIRE(polls == old_polls && links == old_links);
    REQUIRE(net_nic_poll_once() == 0 && released == 6);
    REQUIRE(net_start(NET_NIC_LGY98, 0) == 0 && selected == 2);
    REQUIRE(net_nic_poll_once() == 0 && !saw_peer);
    net_unbind(); REQUIRE(released == 7);
    /* Direct binding borrows ops and must not release a module reference. */
    REQUIRE(net_bind(&ops) == 0);
    net_unbind(); REQUIRE(released == 7);
    REQUIRE(inits == 4 && acquired == 8 && registrations == 4);
    REQUIRE(rx_calls && demuxes && !bad_if && hs_if());
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}

int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_take(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
volatile u32 tick_count;
static void (*rx_idle)(void);
int idle_work_register(void (*fn)(void)) { rx_idle = fn; return 0; }
int nic_test_irq_enabled(void) { return hs_if(); }

int tick_budget_peek(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
