/* B7 only: include the real component; A2 substitutes below model F1 after
 * ACK, MMIO ordering, IRQ save/restore and the polled serial facade. */
#define IO_H
#include "../../hwdev/gf/command.h"
static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 250 + 1)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static unsigned host_flags = 0x202U;
static unsigned irq_save(void)
{ unsigned v = host_flags; host_flags &= ~0x200U; return v; }
static void irq_restore(unsigned v)
{ CHECK(!(host_flags & 0x200U)); host_flags = v; }
#ifndef GF2_FAULT_SOURCE
#define GF2_FAULT_SOURCE "../../hwdev/gf/fault.c"
#endif
#include GF2_FAULT_SOURCE

/* Explicit addresses/values are independent of product register aliases.
 * kind 2 is the F1 request close, strictly after capture and before gates. */
static const struct event { u32 kind, off, value; } expected[] = {
    {0,0x2500,0x11}, {1,0x2500,0}, {0,0x3204,0x10003},
    {0,0x3270,0xffc}, {0,0x3250,0x101},
    {0,0x90ff8,0x6104}, {0,0x90ffc,0xdeadbeef},
    {0,0x3240,0x1234}, {0,0x3244,0x1200},
    {0,0x3220,0x81}, {0,0x3228,0x20000000},
    {0,0x3224,0x30086078}, {0,0x32a0,0x42}, {0,0xc00048,7},
    {0,0x40032c,0x12345000}, {0,0x400704,0x50104},
    {0,0x400708,0xcafef00d}, {0,0x400174,0xdead004a}, {2,0,0},
    {0,0x3220,0x81}, {1,0x3220,0x80}, {0,0x3200,0x91}, {1,0x3200,0x90},
    {0,0x3250,0x101}, {1,0x3250,0x100}, {0,0x400720,0xa5}, {1,0x400720,0xa4}
};
#define EVENT_COUNT (sizeof(expected) / sizeof(expected[0]))
static struct gf_foundation foundation;
static struct gf2_chan channel;
static u32 cursor, fail_at, snapshots, log_bytes;
static int enable_rc, serial_gated;
static char output[1024];
static u32 event(u32 kind, u32 off)
{
    CHECK(cursor < EVENT_COUNT && expected[cursor].kind == kind);
    CHECK(expected[cursor].off == off);
    return expected[cursor++].value;
}
static int mmio_read(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    CHECK(owner == &foundation && bar == 0 && width == 4);
    u32 value = event(0, off);
    if (cursor == fail_at) return -1;
    *out = value; return 0;
}
static int mmio_write(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    CHECK(owner == &foundation && bar == 0 && width == 4);
    CHECK(value == event(1, off));
    return cursor == fail_at ? -1 : 0;
}
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{ CHECK(f == &foundation); snapshots++; *out = f->stats; }
int gf_irq_enable(struct gf_foundation *f, bool fifo, bool graph, unsigned heads)
{
    CHECK(f == &foundation && !fifo && !graph && heads == 3);
    CHECK(!(host_flags & 0x200U)); (void)event(2, 0);
    f->wanted_fifo = f->wanted_graph = 0; f->wanted_heads = heads;
    return enable_rc;
}
int serial_gate_active(void) { return serial_gated; }
void serial_puts_polled(const char *s)
{
    CHECK(cursor == EVENT_COUNT && channel.state == GF2_FAULT);
    while (*s) { CHECK(log_bytes + 1 < sizeof(output)); output[log_bytes++] = *s++; }
    output[log_bytes] = 0;
}
static bool contains(const char *s)
{
    for (u32 i = 0; i < log_bytes; i++) {
        u32 j = 0;
        while (s[j] && output[i + j] && output[i + j] == s[j]) j++;
        if (!s[j]) return true;
    }
    return false;
}
static void reset(void)
{
    foundation = (struct gf_foundation){0}; channel = (struct gf2_chan){0};
    foundation.io.opaque = &foundation;
    foundation.io.read = mmio_read; foundation.io.write = mmio_write;
    foundation.wanted_fifo = foundation.wanted_graph = ~0U;
    channel.f = &foundation; channel.state = GF2_RUN; channel.heads = 3; channel.epoch = 9;
    cursor = fail_at = snapshots = log_bytes = 0; output[0] = 0;
    enable_rc = serial_gated = 0; host_flags = 0x202U;
}
static void b7_fault_order(void)
{
    reset();
    /* F1 has ACKed live status. An unsigned wrap in FIFO alone must fault. */
    channel.seen.fifo = ~0U;
    foundation.stats.nsource = 0x20; foundation.stats.nstatus = 0x40;
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && cursor == EVENT_COUNT);
    CHECK(channel.state == GF2_FAULT && channel.fault.latched && host_flags == 0x202U);
    CHECK(!foundation.wanted_fifo && !foundation.wanted_graph && foundation.wanted_heads == 3);
    CHECK(channel.last_rc == GF2_EFAULT && channel.fault.epoch == 9 && channel.fault.chid == 3);
    CHECK(channel.fault.caches == 0x11 && channel.fault.push1 == 0x10003);
    CHECK(channel.fault.cache_get == 0xffc && channel.fault.pull0 == 0x101);
    CHECK(channel.fault.put == 0x1234 && channel.fault.get == 0x1200 && channel.fault.reference == 7);
    CHECK(channel.fault.method == 0x6104 && channel.fault.data == 0xdeadbeef);
    CHECK(channel.fault.dma_push == 0x81 && channel.fault.dma_state == 0x20000000);
    CHECK(channel.fault.dma_fetch == 0x30086078 && channel.fault.dma_dcount == 0x42);
    CHECK(channel.fault.gr_trap == 0x12345000 && channel.fault.gr_class == 0x4a);
    CHECK(channel.fault.gr_method == 0x50104 && channel.fault.gr_data == 0xcafef00d);
    CHECK(contains("pfifo=00000000 pgraph=00000000 nsource=00000020 nstatus=00000040"));
    CHECK(contains("subc=3 method=0104 data=deadbeef"));
    CHECK(contains("gr_class=004a gr_subc=5 gr_method=0104 gr_data=cafef00d"));
    u32 logged = log_bytes;
    foundation.stats = (struct gf_irq_stats){.entries=4, .fifo=2, .graph=1,
        .pmc=0x1100, .pfifo=1, .pgraph=2, .nsource=4, .nstatus=8, .unknown=0x80000000};
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && cursor == EVENT_COUNT);
    CHECK(channel.fault.first.fifo == 0 && channel.fault.first.nsource == 0x20);
    CHECK(channel.fault.latest.fifo == 2 && channel.fault.latest.graph == 1);
    CHECK(channel.fault.latest.pmc == 0x1100 && channel.fault.latest.pfifo == 1);
    CHECK(channel.fault.latest.pgraph == 2 && channel.fault.latest.nsource == 0x24);
    CHECK(channel.fault.latest.nstatus == 0x48 && channel.fault.latest.unknown == 0x80000000);
    CHECK(channel.fault.method == 0x6104 && channel.fault.gr_class == 0x4a);
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && log_bytes == logged && snapshots == 3);
}
void _start(void)
{
    b7_fault_order();
    reset(); foundation.stats.entries = 3; foundation.stats.vblank[0] = 4;
    CHECK(gf2_fault_check(&channel) == GF2_OK && !cursor && !channel.fault.latched);
    CHECK(channel.seen.vblank[0] == 4);
    foundation.stats.graph = 1; serial_gated = 1;
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && cursor == EVENT_COUNT && !log_bytes);
    /* Failed request close / gate write still attempts the remaining gates. */
    reset(); foundation.stats.graph = 1; enable_rc = GF1_EIO;
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && cursor == EVENT_COUNT);
    CHECK(channel.last_rc == GF2_EIO && channel.state == GF2_FAULT);
    reset(); foundation.stats.fifo = 1; fail_at = 21;
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && cursor == EVENT_COUNT);
    CHECK(channel.last_rc == GF2_EIO && channel.state == GF2_FAULT);
    reset(); channel.state = GF2_FAULT;
    CHECK(gf2_fault_check(&channel) == GF2_EFAULT && !cursor);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
