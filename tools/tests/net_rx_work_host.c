/* Real finite executor, NIC lifetime, queues and idle runner; fake device/time. */
#define IO_H
#include "types.h"
#include "os32_kapi_shared.h"
#include "kernel/irq/tick.h"
#include "kernel/irq/irq.h"
static unsigned int enabled = 1;
static unsigned int irq_save(void) { unsigned int f = enabled; enabled = 0; return f; }
static void irq_restore(unsigned int f) { enabled = f; }
static int _irq_enabled(void) { return enabled != 0; }
#include "net/nic.c"
#include "net/nic_rx.c"
#include "net/rx_work.c"
#include "kernel/idle_work.c"
#include <stdarg.h>

#define CHECK(e) do { if (!(e)) finish(1); } while (0)
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
volatile u32 tick_count, kctx_irq_depth;
u32 link_hello_ok;
u8 link_peer_mac[6];
static u32 now, step, clock_cost, frames, reads, polls, links, released;
static int clock_fail, allowance = 1, hook, rescue, idle_error, recv_reentry, valid_frame, copy_action;
static tick_fn callback;
static const u8 hw_mac[6] = {2,3,4,5,6,7};
static int tx(const void *p, u32 n) { (void)p; (void)n; return 0; }
static int recv_frame(void *p, u32 cap, u32 *n)
{
    CHECK(enabled ? kctx_irq_depth == 0 : rescue);
    CHECK(cap >= 60);
    reads++;
    if (recv_reentry) {
        recv_reentry = 0;
        u32 before = reads;
        CHECK(net_rx_service() == 0 && reads == before);
    }
    if (!frames) return OS32_ERR_AGAIN;
    frames--;
    now += step;
    /* Discarded Ethernet still consumes a frame of the budget. */
    *n = 0;
    if (valid_frame) {
        u8 *f = p;
        for (u32 i = 0; i < 60; i++) f[i] = 0;
        for (u32 i = 0; i < 6; i++) f[i] = hw_mac[i];
        f[12] = OS32_ETHERTYPE >> 8; f[13] = OS32_ETHERTYPE & 255;
        f[14] = LINK_OP_HELLO;
        *n = 60;
    }
    return 0;
}
static void get_mac(u8 *p) { for (u32 i = 0; i < 6; i++) p[i] = hw_mac[i]; }
static void device_poll(void) { CHECK(!enabled); polls++; }
static u32 device_credit(void) { return 100; }
static const struct NetNicOps device = {tx, recv_frame, get_mac, device_poll, device_credit};
static int get_device(const struct NetNicOps **out) { *out = &device; return 0; }
static void put_device(void) { CHECK(enabled); released++; }
static const struct NetAdapter e_adapter = {get_device, put_device};
static const struct NetAdapter n_adapter = {get_device, put_device};
void __cdecl kprintf(u8 a, const char *fmt, ...) { (void)a; (void)fmt; }
int tick_register(unsigned int slot, tick_fn fn)
{ CHECK(slot == PC98_TICK_LINK); callback = fn; return 0; }
void link_init(const u8 p[6]) { (void)p; }
static void irq_tick(void)
{
    unsigned int f = irq_save();
    kctx_irq_depth++;
    callback();
    kctx_irq_depth--;
    irq_restore(f);
}
void link_tick(void)
{
    CHECK(!enabled);
    links++;
    u32 before = reads;
    CHECK(net_nic_poll_once() == 0 && reads == before);
}
void *kmemcpy(void *d, const void *s, u32 n)
{
    if (copy_action && d == link_queue.frames[0]) {
        int action = copy_action;
        copy_action = 0;
        CHECK(enabled && ticking && !net_rx_queued(NET_RX_LINK));
        if (action == 1) net_rx_reset(NET_RX_LINK);
        if (action == 2) net_unbind();
        CHECK(bound && ticking);
    }
    for (u32 i = 0; i < n; i++) ((u8 *)d)[i] = ((const u8 *)s)[i];
    return d;
}
int platform_clock_read_us(u32 *lo, u32 *hi)
{
    CHECK(!enabled);
    now += clock_cost;
    if (hook) {
        int action = hook;
        hook = 0;
        /* Model an IRQ interrupting the cooperative demux, then reentry. */
        irq_tick();
        u32 before = reads;
        CHECK(net_rx_service() == 0 && reads == before);
        if (action == 2) net_rx_reset(NET_RX_LINK);
        if (action == 3) net_unbind();
        if (action == 4) { u32 age = last_idle_jiffy; net_rx_idle(); CHECK(last_idle_jiffy == age); }
    }
    if (clock_fail) return OS32_ERR_AGAIN;
    *lo = now; *hi = 0;
    return 0;
}
u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_take(u32 share, u32 tail)
{
    CHECK(share == IRQ0_SLOT_BUDGET_US / 2 && tail == NET_TAIL_RX_US);
    return allowance && (!rescue || now < 37);
}
static void idle_other(void) {}
static void run_tests(void)
{
    CHECK(net_adapter_register(NET_NIC_E82557, &e_adapter) == 0);
    CHECK(net_adapter_register(NET_NIC_LGY98, &n_adapter) == 0);
    /* No bound NIC may be published without its permanent idle slot. */
    work_count = 4;
    CHECK(net_start(NET_NIC_E82557, 0) == OS32_ERR_FULL && !bound);
    CHECK(released == 1);
    work_count = 0;
    CHECK(idle_work_register(idle_other) == 0);
    CHECK(net_start(NET_NIC_E82557, 0) == 0 && callback == net_jiffy);
    CHECK(work_count == 2);
    frames = 9;
    CHECK(net_rx_service() == 8 && frames == 1 && pending);
    CHECK(!polls);
    idle_work_run(); CHECK(frames == 0 && !pending);
    frames = 9; step = 120;
    CHECK(net_rx_service() == 1 && frames == 8 && pending);
    step = 0; idle_work_run(); CHECK(!frames);
    /* Nested batches inherit one budget across enqueue/service calls. */
    frames = 9;
    net_rx_batch_begin(); net_rx_service(); net_rx_batch_begin();
    for (u32 i = 0; i < 9; i++) net_rx_service();
    CHECK(frames == 1);
    net_rx_batch_end(); net_rx_service(); CHECK(frames == 1);
    net_rx_batch_end(); idle_work_run(); CHECK(!frames);
    frames = 1;
    enabled = 0; u32 before = reads;
    CHECK(net_rx_service() == 0 && reads == before && !enabled);
    enabled = 1; kctx_irq_depth = 1;
    CHECK(net_nic_poll_once() == 0 && reads == before);
    kctx_irq_depth = 0; idle_work_run(); CHECK(!frames);
    recv_reentry = 1; frames = 1; CHECK(net_rx_service() == 1);
    hook = 1; before = links; frames = 1;
    CHECK(net_rx_service() == 1 && links == before + 1 && enabled);
    hook = 2; net_rx_service(); CHECK(!reset_due);
    hook = 4; tick_count++; net_rx_service();
    /* Rescue age 1/2, empty, repeat, idle return, wrap and rebind. */
    idle_work_run(); frames = 1; now = 0; rescue = 1;
    tick_count++; before = reads; irq_tick(); CHECK(reads == before);
    tick_count++; irq_tick(); CHECK(frames == 0 && reads == before + 2);
    before = reads; tick_count++; irq_tick(); CHECK(reads == before + 1);
    rescue = 0; idle_work_run(); rescue = 1; now = 0;
    before = reads; tick_count++; irq_tick(); CHECK(reads == before);
    tick_count++; allowance = 0; irq_tick(); CHECK(reads == before);
    allowance = 1;
    /* Clock read costs must be included in the IRQ RX allowance. */
    now = 0; clock_cost = 20; frames = 2;
    irq_tick(); CHECK(frames == 2 && pending);
    clock_cost = 0; now = 0; frames = 9;
    irq_tick(); CHECK(frames == 1 && pending);
    rescue = 0; idle_work_run(); CHECK(!frames);
    net_unbind(); tick_count = 0xffffffffu;
    CHECK(net_start(NET_NIC_E82557, 0) == 0);
    frames = 1; before = reads; rescue = 1; now = 0;
    tick_count = 0; irq_tick(); CHECK(reads == before);
    tick_count = 1; irq_tick(); CHECK(!frames);
    rescue = 0; net_unbind();
    CHECK(net_start(NET_NIC_LGY98, 0) == 0);
    tick_count += 2; frames = 1; before = reads; irq_tick(); CHECK(reads == before);
    idle_work_run(); CHECK(!frames);
    /* Failed clock uses frame cap, including low-word time wrap. */
    now = 0xfffffff0u; clock_fail = 1; frames = 9;
    CHECK(net_rx_service() == 8 && frames == 1 && net_rx_clock_failures);
    clock_fail = 0; step = 120; frames = 9;
    CHECK(net_rx_service() == 1 && frames == 8);
    step = 0; frames = 0;
    valid_frame = 1; frames = 1; copy_action = 1;
    CHECK(net_rx_service() == 1 && !net_rx_queued(NET_RX_LINK));
    frames = 1; copy_action = 2; before = released;
    CHECK(net_rx_service() == 1 && !bound && released == before + 1);
    valid_frame = 0;
    CHECK(net_start(NET_NIC_E82557, 0) == 0);
    hook = 3; before = released;
    net_rx_service(); CHECK(!bound && released == before + 1);
    CHECK(enabled && !kctx_irq_depth && !idle_error);
}
void _start(void) { run_tests(); finish(0); }
