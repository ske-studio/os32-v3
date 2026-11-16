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
static void _idle(void) {}
#include "kernel/io_work.h"
static void nic_request(u32 id);
#define io_request nic_request
#include "net/nic.c"
#undef io_request
static u32 requests;
static void nic_request(u32 id) { ++requests; io_request(id); }
#include "net/nic_rx.c"
#include "net/rx_work.c"
#include "kernel/idle_work.c"
#include "kernel/irq/tick_budget.c"
#define TEST_RX_CLOCK_LOCK irq_save
#define TEST_RX_CLOCK_UNLOCK irq_restore
#include "rx_io_clock.h"

static void fail(u32 line);
#define CHECK(e) do { if (!(e)) fail(__LINE__); } while (0)
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void fail(u32 line)
{
    char buf[16]; u32 n = 0;
    buf[n++] = '\n';
    do { buf[n++] = '0' + line % 10; line /= 10; } while (line);
    for (u32 i = 0; i < n / 2; ++i) {
        char c = buf[i]; buf[i] = buf[n - i - 1]; buf[n - i - 1] = c;
    }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(buf), "d"(n) : "memory");
    finish(1);
}
volatile u32 tick_count, kctx_irq_depth;
u32 link_hello_ok;
u8 link_peer_mac[6];
static u32 now, step, clock_cost, frames, reads, polls, links, released;
static int clock_fail, allowance = 1, hook, rescue, idle_error, recv_reentry, valid_frame, copy_action;
static u32 irq_elapsed, link_received;
static int pump_link, quiet, fg_ms_override;
static u32 fg_ms;
static int unrelated_type, wrong_destination;
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
        if (unrelated_type) { f[12] = 0x08; f[13] = 0x06; } /* ARP */
        if (wrong_destination) f[0] ^= 0x10;
        *n = 60;
    }
    return 0;
}
static void get_mac(u8 *p) { for (u32 i = 0; i < 6; i++) p[i] = hw_mac[i]; }
static void device_poll(void)
{
    CHECK(!enabled && ticking);
    u32 before = reads;
    if (!quiet) CHECK(net_rx_service() == 0 && reads == before);
    polls++;
}
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
    u32 old_polls = polls;
    int busy = ticking;
    kctx_irq_depth++;
    if (rescue) { tick_budget_begin(); now += irq_elapsed; }
    callback();
    CHECK(polls == old_polls + (busy ? 0u : 1u));
    if (rescue) tick_budget_end();
    kctx_irq_depth--;
    irq_restore(f);
}
void link_tick(void)
{
    CHECK(!enabled);
    links++;
    /* Same queued-frame/take ordering as link_rx_pump. */
    if (pump_link) {
        u8 frame[60]; u32 len;
        while (net_rx_queued(NET_RX_LINK) &&
               tick_budget_take(tick_budget_cnt(IRQ0_SLOT_BUDGET_US / 2),
                                tick_budget_cnt(NET_TAIL_RX_US))) {
            CHECK(net_rx_take(NET_RX_LINK, frame, sizeof(frame), &len) == 0);
            link_received++;
        }
    }
    u32 before = reads;
    if (!quiet) CHECK(net_nic_poll_once() == 0 && reads == before);
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
        if (action == 5) irq_tick(); /* Coalesce multiple missed polls. */
        u32 before = reads;
        CHECK(net_rx_service() == 0 && reads == before);
        if (action == 2) net_rx_reset(NET_RX_LINK);
        if (action == 3) net_unbind();
        if (action == 4) { u32 age = last_idle_jiffy; net_rx_idle(0, &batch_budget.local); CHECK(last_idle_jiffy == age); }
    }
    if (clock_fail) return OS32_ERR_AGAIN;
    *lo = now; *hi = 0;
    return 0;
}
u32 arch_tick_budget_cnt(u32 us) { return us; }
u32 arch_tick_budget_base(void) { return now; }
u32 arch_tick_budget_elapsed(u32 origin)
{
    return allowance ? (u32)(now - origin) : IRQ0_SLOT_BUDGET_US;
}
static void idle_other(void) {}
static void test_deferred_poll(void)
{
    u32 old_polls = polls, old_links = links;
    /* Even at rescue age, an interrupted foreground owns RX until finish. */
    tick_count = last_idle_jiffy + LINK_RX_IDLE_RESCUE_JIFFIES;
    rescue = 1; hook = 5; frames = 1;
    CHECK(net_rx_service() == 1 && enabled && !ticking);
    CHECK(polls == old_polls + 1 && links == old_links + 1);
    CHECK(!poll_due && !link_due && pending);
    net_rx_service();
    CHECK(polls == old_polls + 1 && !pending);
    irq_tick(); CHECK(polls == old_polls + 2 && !poll_due);
    rescue = 0;
}
static void tier_requests(void)
{
    quiet = 1;
    work_count = 0;
    (void)io_dispatch(500); /* Consume the preceding unbind request. */
    CHECK(io_dispatch(500) == 0); /* Unbound DONE sleeps without a request. */
    net_rx_note(); CHECK(net_rx_recent(5));
    CHECK(net_bind(&device) == 0 && !net_rx_recent(5));
    fg_ms_override = 1; fg_ms = tick_count * 10u;
    frames = 0;
    fg_ms += NET_RX_ACTIVE_MS; /* Prior RX has expired. */
    for (u32 i = 0; i < 3; ++i) (void)io_dispatch(500);
    CHECK(io_dispatch(500) == 1); /* Bound poll-only NICs stay requested. */
    u32 old_polls = polls, old_links = links, old_jiffy = tick_count;
    CHECK(io_dispatch(500) == 1 && polls == old_polls && links == old_links);
    ++fg_ms;
    CHECK(io_dispatch(500) == 1 && polls == old_polls + 1 && links == old_links + 1);
    CHECK(tick_count == old_jiffy); /* 1ms progress, before the next 10ms jiffy. */
    valid_frame = 1;
    frames = 2; u32 before = reads;
    CHECK(io_dispatch(500) == 1 && frames < 2 && reads > before);
    /* The Tier fairness reservation can grant just one unit in a pass. */
    CHECK(io_dispatch(500) == 1 && frames == 0);
    CHECK(net_rx_recent(5) && !net_rx_recent(0));
    old_polls = polls; old_links = links;
    CHECK(io_dispatch(500) == 1 && polls == old_polls + 1 && links == old_links);
    fg_ms += 4;
    CHECK(net_rx_recent(5));
    (void)io_dispatch(500); old_polls = polls; old_links = links;
    CHECK(io_dispatch(500) == 1 && polls == old_polls + 1 && links == old_links);
    ++fg_ms;
    CHECK(!net_rx_recent(5));
    (void)io_dispatch(500); old_polls = polls; old_links = links;
    CHECK(io_dispatch(500) == 1 && polls == old_polls && links == old_links);
    fg_ms = 0xfffffffeu; frames = 1;
    CHECK(io_dispatch(500) == 1 && !frames && net_rx_recent(5));
    fg_ms = 2; CHECK(net_rx_recent(5));
    fg_ms = 3; CHECK(!net_rx_recent(5));
    fg_ms = 0x7ffffffeu; CHECK(!net_rx_recent(5)); /* Age 0x80000000 is not recent. */
    /* Unrelated EtherTypes and destinations never start or extend activity. */
    fg_ms = 10; unrelated_type = 1; frames = 1;
    CHECK(net_rx_service() == 1 && !net_rx_recent(5));
    unrelated_type = 0; wrong_destination = 1; frames = 1;
    CHECK(net_rx_service() == 1 && !net_rx_recent(5));
    wrong_destination = 0; frames = 1;
    CHECK(net_rx_service() == 1 && net_rx_recent(5));
    fg_ms = 14; unrelated_type = 1; frames = 1;
    CHECK(net_rx_service() == 1 && net_rx_recent(5));
    fg_ms = 15; CHECK(!net_rx_recent(5));
    unrelated_type = valid_frame = 0;
    u32 marks = requests;
    net_rx_schedule(); CHECK(requests == marks + 1);
    CHECK(io_dispatch(500) == 1 && frames == 0);
    CHECK(io_dispatch(500) == 1);
    /* Binding and both forms of unbinding are explicit wakeup sources. */
    marks = requests; net_unbind(); CHECK(requests == marks + 1);
    CHECK(io_dispatch(500) == 1 && io_dispatch(500) == 0); /* DONE unbound. */
    marks = requests; CHECK(net_bind(&device) == 0 && requests == marks + 1);
    frames = 1; CHECK(io_dispatch(500) == 1 && frames == 0);
    CHECK(io_dispatch(500) == 1);
    marks = requests; ticking = 1; net_unbind(); ticking = 0;
    CHECK(requests == marks + 1 && unbind_due);
    CHECK(io_dispatch(500) == 1 && !bound);
    CHECK(net_bind(&device) == 0); (void)io_dispatch(500);
    /* Tick while busy can only defer poll; it must still wake the provider. */
    ticking = 1; marks = requests; net_tick(); ticking = 0;
    CHECK(requests == marks + 1 && poll_due);
    frames = 1; CHECK(io_dispatch(500) == 1 && frames == 0 && !poll_due);
    (void)io_dispatch(500); CHECK(io_dispatch(500) == 1);
    marks = requests; net_jiffy(); CHECK(requests == marks + 3);
    CHECK(io_dispatch(500) == 1);
    (void)io_dispatch(500); CHECK(io_dispatch(500) == 1);
    marks = requests; frames = 1; net_tx(hw_mac, 6);
    CHECK(requests == marks + 1 && frames == 0);
    CHECK(io_dispatch(500) == 1);
    /* Narrow the dispatcher pass below 120us; MORE resumes next pass. */
    frames = 3; step = 20; net_rx_schedule();
    CHECK(io_dispatch(10) == 1 && frames == 2);
    CHECK(io_dispatch(10) == 1 && frames == 1);
    step = 0; CHECK(io_dispatch(500) == 1 && frames == 0);
    CHECK(io_dispatch(500) == 1);
    /* Failure before dispatch and failure at the next frame both start none. */
    frames = 1; net_rx_schedule(); clock_fail = 1; before = reads;
    CHECK(io_dispatch(500) == 0 && reads == before && frames == 1);
    clock_fail = 0; CHECK(io_dispatch(500) == 1 && frames == 0);
    /* Provider reentry returns MORE without reading or consuming the slice. */
    struct io_slice slice;
    CHECK(io_slice_begin(&slice, 120, 8) == 0);
    ticking = 1; before = reads;
    CHECK(net_rx_idle(0, &slice) == IO_MORE && reads == before);
    ticking = 0;
    CHECK(io_slice_take(&slice));
    frames = 9;
    CHECK(io_slice_begin(&slice, 120, 8) == 0);
    CHECK(net_rx_idle(0, &slice) == IO_MORE && frames == 1);
    CHECK(io_dispatch(500) == 1 && frames == 0);
    CHECK(io_dispatch(500) == 1);
    ticking = 1; io_request(IO_SVC_NET_RX);
    CHECK(io_dispatch(500) == 1);
    ticking = 0; frames = 1;
    CHECK(io_dispatch(500) == 1 && frames == 0); /* MORE re-requests a busy provider. */
    net_unbind(); (void)io_dispatch(500);
    CHECK(io_dispatch(500) == 0);
    fg_ms_override = 0;
}
static void run_tests(void)
{
    tick_budget_config();
    CHECK(net_adapter_register(NET_NIC_E82557, &e_adapter) == 0);
    CHECK(net_adapter_register(NET_NIC_LGY98, &n_adapter) == 0);
    /* No bound NIC may be published if its resident Tier registration fails. */
    struct io_desc filler = rx_desc;
    filler.id = IO_SVC_NE2K_BH;
    CHECK(io_register(&filler) == 0);
    CHECK(io_register(&rx_desc) == 0);
    CHECK(net_start(NET_NIC_E82557, 0) == OS32_ERR_EXIST && !bound);
    CHECK(released == 1);
    CHECK(io_unregister(IO_SVC_NE2K_BH) == 0);
    CHECK(io_unregister(IO_SVC_NET_RX) == 0);
    CHECK(idle_work_register(idle_other) == 0);
    CHECK(net_start(NET_NIC_E82557, 0) == 0 && callback == net_jiffy);
    CHECK(work_count == 1 && !net_rx_recent(5));
    test_deferred_poll(); polls = 0;
    frames = 9;
    CHECK(net_rx_service() == 8 && frames == 1 && pending);
    CHECK(!polls);
    /* link_tick's rejected recursive RX request schedules pending again;
     * draining the last frame therefore need not leave pending clear. */
    idle_work_run(); CHECK(frames == 0 && polls == 1 && pending);
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
    if (!quiet) CHECK(net_nic_poll_once() == 0 && reads == before);
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
    clock_cost = 0;
    /* RX turn: entrance at 30us, two 4us reads cross 60-23us.
     * Optional NIC rescue must preserve the queued LINK first unit. */
    rescue = 0; valid_frame = 1; frames = 1;
    CHECK(net_rx_service() == 1 && net_rx_queued(NET_RX_LINK) == 1);
    valid_frame = 0; frames = 1; rescue = 1; pump_link = 1;
    tick_count = (last_idle_jiffy + 3u) & ~1u;
    irq_elapsed = 30; clock_cost = 4; before = reads;
    irq_tick();
    CHECK(reads == before && frames == 1 && link_received == 1);
    CHECK(!net_rx_queued(NET_RX_LINK));
    /* Entrance already beyond the budget also preserves the guarantee. */
    rescue = 0; clock_cost = 0; valid_frame = 1;
    CHECK(net_rx_service() == 1 && net_rx_queued(NET_RX_LINK) == 1);
    valid_frame = 0; frames = 1; rescue = 1; irq_elapsed = 120;
    irq_tick(); CHECK(frames == 1 && link_received == 2);
    pump_link = 0; irq_elapsed = 0;
    clock_cost = 0; now = 0; frames = 9;
    irq_tick(); CHECK(frames == 1 && pending);
    rescue = 0; idle_work_run(); CHECK(!frames);
    net_unbind(); tick_count = 0xffffffffu;
    CHECK(net_start(NET_NIC_E82557, 0) == 0);
    frames = 1; before = reads; rescue = 1; now = 0;
    tick_count = 0; irq_tick(); CHECK(reads == before);
    valid_frame = 1;
    tick_count = 1; irq_tick(); CHECK(!frames && net_rx_recent(5));
    valid_frame = 0;
    rescue = 0; net_unbind(); CHECK(!net_rx_recent(5));
    CHECK(net_start(NET_NIC_LGY98, 0) == 0);
    test_deferred_poll();
    tick_count += 2; frames = 1; before = reads;
#if LINK_PLATFORM_TICK_RX_LGY98_SLOT
    rescue = 1; now = 0; irq_tick(); CHECK(reads > before && !frames); rescue = 0;
#else
    irq_tick(); CHECK(reads == before);
#endif
    idle_work_run(); CHECK(!frames);
    /* P8: a failed raw clock starts no unit; low-word wrap still works. */
    now = 0xfffffff0u; clock_fail = 1; frames = 9;
    CHECK(net_rx_service() == 0 && frames == 9 && net_rx_clock_failures);
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
    /* The first unit is guaranteed even when the window elapsed before it. */
    {
        struct rx_work_budget late;
        u32 saved = step; step = 0;
        rx_work_begin(&late); now += 5000;
        CHECK(rx_work_take(&late)); CHECK(!rx_work_take(&late));
        step = saved;
    }
    /* P10: device units consume the shared slice, not the demux's eight. */
    struct io_slice shared;
    struct rx_work_budget capped;
    CHECK(io_slice_begin(&shared, 120, 30) == 0);
    CHECK(io_slice_take(&shared) && io_slice_take(&shared));
    rx_work_from_slice_capped(&capped, &shared);
    for (unsigned int i = 0; i < NET_RX_SCAN_MAX; i++) CHECK(rx_work_take(&capped));
    CHECK(!rx_work_take(&capped) && shared.units == NET_RX_SCAN_MAX + 2);
    tier_requests();
}
void _start(void) { test_rx_clock_install(); run_tests(); finish(0); }

u32 tick_clock_ms(void) { return fg_ms_override ? fg_ms : tick_count * 10u; }
