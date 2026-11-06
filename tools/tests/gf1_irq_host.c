/* Real B2 code, fake level MMIO and shared IRQ registration. No native ports. */
#define IO_H
#include "types.h"
#include "hwdev/gf/foundation.h"
#include "kernel/irq/irq.h"
#include "drivers/pci/pci.h"
static u32 host_if = 0x202U, saves, restores;
static void fail(int line)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 250 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static unsigned irq_save(void)
{ unsigned v = host_if; host_if &= ~0x200U; saves++; return v; }
static void irq_restore(unsigned v)
{ CHECK(!(host_if & 0x200U)); host_if = v; restores++; }
#ifndef GF1_IRQ_SOURCE
#define GF1_IRQ_SOURCE "../../hwdev/gf/irq.c"
#endif
#include GF1_IRQ_SOURCE

struct reg { u32 off, value, sticky; };
/* Explicit addresses form an independent oracle for GR enable and ACKs. */
static struct reg regs[] = {
    {0x100,0,0}, {0x140,0,0}, {0x2100,0,0}, {0x2140,0,0},
    {0x400100,0,0}, {0x40013c,0,0}, {0x400104,0,0}, {0x400108,0,0},
    {0x400160,0,0}, {0x40032c,0,0}, {0x400704,0,0}, {0x400708,0,0}, {0x400174,0,0},
    {0x600100,0,0}, {0x600140,0,0}, {0x602100,0,0}, {0x602140,0,0},
    {0x8100,0,0}, {0x8140,0,0}, {0x9100,0,0}, {0x9140,0,0},
    {0x1100,0,0}, {0x1140,0,0}, {0xb100,0,0}, {0xb140,0,0}
};
static struct gf_foundation f;
static u32 ops, writes, unknown, cfg_reads, logs, register_calls, unregister_calls;
static int register_rc, unregister_rc;
static u32 fault_off, ack_fifo, ack_graph, ack_head[2], sequence[1024], seq_count;
static bool live, in_irq, aggregate_hidden;
static irq_handler_fn registered_fn;
static void *registered_arg;
static struct reg *reg_at(u32 off)
{
    for (u32 i = 0; i < sizeof(regs)/sizeof(regs[0]); i++)
        if (regs[i].off == off) return &regs[i];
    CHECK(0); return 0;
}
static u32 pmc_status(void)
{
    u32 v = unknown;
    if (aggregate_hidden) return v;
    if (reg_at(0x2100)->value & reg_at(0x2140)->value) v |= 0x100;
    if (reg_at(0x400100)->value & reg_at(0x40013c)->value) v |= 0x1000;
    if (reg_at(0x600100)->value & reg_at(0x600140)->value) v |= 0x01000000;
    if (reg_at(0x602100)->value & reg_at(0x602140)->value) v |= 0x02000000;
    if (reg_at(0x8100)->value & reg_at(0x8140)->value) v |= 0x10000;
    if (reg_at(0x9100)->value & reg_at(0x9140)->value) v |= 0x100000;
    if (reg_at(0x1100)->value & reg_at(0x1140)->value) v |= 0x10000000;
    if (reg_at(0xb100)->value & reg_at(0xb140)->value) v |= 1;
    return v;
}
static int read_mmio(void *arg, u32 bar, u32 off, u32 width, u32 *out)
{
    CHECK(arg == &f && live && bar == 0 && width == 4);
    ops++;
    if (off == fault_off) return -1;
    *out = off == 0x100 ? pmc_status() : reg_at(off)->value;
    return 0;
}
static bool is_status(u32 off)
{
    return off == 0x2100 || off == 0x400100 || off == 0x600100 ||
           off == 0x602100 || off == 0x8100 || off == 0x9100 ||
           off == 0x1100 || off == 0xb100;
}
static int write_mmio(void *arg, u32 bar, u32 off, u32 width, u32 value)
{
    CHECK(arg == &f && live && bar == 0 && width == 4);
    CHECK(off != 0x100 && off != 0x400720 && off != 0x2500);
    ops++; writes++;
    CHECK(seq_count < 1024); sequence[seq_count++] = off;
    if (off == fault_off) return -1;
    struct reg *r = reg_at(off);
    if (is_status(off)) {
        r->value &= ~value;
        r->value |= r->sticky;
        if (off == 0x2100) ack_fifo = value;
        if (off == 0x400100) {
            ack_graph = value;
            /* Trap registers may change after W1C; the saved trap must not. */
            reg_at(0x40032c)->value = reg_at(0x400704)->value = 0;
            reg_at(0x400708)->value = reg_at(0x400174)->value = 0;
        }
        if (off == 0x600100) ack_head[0] = value;
        if (off == 0x602100) ack_head[1] = value;
    } else r->value = value;
    return 0;
}
u8 pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 off)
{
    CHECK(!in_irq && bus == 0 && dev == 13 && fn == 0 && off == 0x3c);
    cfg_reads++; return 6;
}
void serial_puts_polled(const char *s)
{ CHECK(!in_irq && s); logs++; }
int serial_gate_active(void)
{ CHECK(!in_irq); return 0; }
int irq_register(unsigned irq, irq_handler_fn fn, void *arg, unsigned flags)
{
    CHECK(!in_irq && irq == 6 && fn == gf_irq_handler && arg == &f && flags == IRQ_F_SHARED);
    CHECK(!reg_at(0x140)->value && !reg_at(0x2140)->value && !reg_at(0x40013c)->value);
    CHECK(!reg_at(0x600140)->value && !reg_at(0x602140)->value);
    /* Another device is already on the line: shared registration is required. */
    register_calls++;
    if (!register_rc) { registered_fn = fn; registered_arg = arg; }
    return register_rc;
}
int irq_unregister(unsigned irq, irq_handler_fn fn, void *arg)
{
    CHECK(!in_irq && irq == 6 && fn == registered_fn && arg == registered_arg);
    CHECK(!f.running);
    unregister_calls++;
    if (!unregister_rc) { registered_fn = 0; registered_arg = 0; }
    return unregister_rc;
}
static void clear_state(void)
{
    f = (struct gf_foundation){0};
    for (u32 i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) regs[i].value = regs[i].sticky = 0;
    f.io.opaque = &f; f.io.read = read_mmio; f.io.write = write_mmio;
    live = true; fault_off = ~0U; unknown = 0; aggregate_hidden = false;
    registered_fn = 0; registered_arg = 0; register_rc = unregister_rc = 0;
    register_calls = unregister_calls = cfg_reads = logs = ops = writes = 0;
    seq_count = 0; ack_fifo = ack_graph = ack_head[0] = ack_head[1] = 0;
}
static void enable(bool fifo, bool graph, u32 heads)
{
    unsigned saved = irq_save();
    f.running = true;
    CHECK(gf_irq_enable(&f, fifo, graph, heads) == GF1_OK);
    irq_restore(saved);
    CHECK(reg_at(0x140)->value == 1 && sequence[seq_count-1] == 0x140);
    CHECK(reg_at(0x2140)->value == (fifo ? ~0U : 0));
    CHECK(reg_at(0x40013c)->value == (graph ? ~0U : 0));
    CHECK(reg_at(0x600140)->value == (heads & 1));
    CHECK(reg_at(0x602140)->value == ((heads >> 1) & 1));
}
static int irq(void)
{
    in_irq = true;
    u32 before = ops, pci_before = cfg_reads, log_before = logs;
    int rc = gf_irq_handler(6, &f);
    CHECK(ops - before < 100 && cfg_reads == pci_before && logs == log_before);
    in_irq = false; return rc;
}
static void poll(void)
{
    in_irq = true;
    u32 before = ops, pci_before = cfg_reads, log_before = logs;
    gf_tick_poll(&f);
    CHECK(ops - before < 100 && cfg_reads == pci_before && logs == log_before);
    in_irq = false;
}
static void registration(void)
{
    clear_state();
    for (u32 i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) regs[i].value = ~0U;
    CHECK(gf_irq_attach(&f) == GF1_OK && f.registered && f.irq_line == 6);
    CHECK(register_calls == 1 && cfg_reads == 1 && logs > 0);
    CHECK(!reg_at(0x8100)->value && !reg_at(0x9100)->value && !reg_at(0x1100)->value && !reg_at(0xb100)->value);
    unregister_rc = IRQ_ERR_CTX;
    CHECK(gf_irq_detach(&f) == GF1_EIRQ && f.registered);
    unregister_rc = 0;
    CHECK(gf_irq_detach(&f) == GF1_OK && !f.registered);
    CHECK(gf_irq_detach(&f) == GF1_OK && unregister_calls == 2);
    clear_state(); register_rc = IRQ_ERR_BUSY;
    CHECK(gf_irq_attach(&f) == GF1_EIRQ && !f.registered && f.last_rc == GF1_EIRQ);
    clear_state(); fault_off = 0x140;
    CHECK(gf_irq_attach(&f) == GF1_EIO && !register_calls);
}
static void dispatch(void)
{
    clear_state(); CHECK(gf_irq_attach(&f) == GF1_OK); enable(true, true, 1);
    CHECK(irq() == IRQ_NONE && f.stats.entries == 1 && reg_at(0x140)->value == 1);
    reg_at(0x2100)->value = 0x15; reg_at(0x2140)->value = 5;
    reg_at(0x400100)->value = 0x80;
    reg_at(0x400108)->value = 0x123; reg_at(0x400104)->value = 0x456;
    reg_at(0x40032c)->value = 0x12345;
    reg_at(0x400704)->value = 0x50104;
    reg_at(0x400708)->value = 0xcafef00d;
    reg_at(0x400174)->value = 0xdead004a;
    CHECK(irq() == IRQ_HANDLED);
    CHECK(f.stats.fifo == 1 && f.stats.graph == 1 && f.stats.pfifo == 5 && ack_fifo == 5);
    CHECK(reg_at(0x2100)->value == 0x10 && ack_graph == 0x80);
    CHECK(f.stats.pgraph == 0x80 && f.stats.nsource == 0x123 && f.stats.nstatus == 0x456);
    CHECK(f.stats.pmc == 0x1100 && reg_at(0x140)->value == 1);
    /* Tick must recover requested status even with no aggregate indication. */
    aggregate_hidden = true; reg_at(0x400100)->value = 0x20;
    poll(); CHECK(!reg_at(0x2100)->value && !reg_at(0x400100)->value);
    CHECK(reg_at(0x2140)->value == ~0U && f.stats.graph == 2);
    /* A clean poll must not erase the first fault before F2 observes it. */
    poll();
    CHECK(!f.stats.pfifo && !f.stats.pgraph);
    CHECK(f.stats.first.pfifo == 5 && f.stats.first.pgraph == 0x80);
    CHECK(f.stats.first.nsource == 0x123 && f.stats.first.nstatus == 0x456);
    CHECK(f.stats.first.gr_trap == 0x12345 && f.stats.first.gr_method == 0x50104);
    CHECK(f.stats.first.gr_data == 0xcafef00d && f.stats.first.gr_class == 0x4a);
    CHECK(f.stats.first.pmc == 0x1100);
    enable(true, true, 3); /* changing heads does not begin a new period */
    CHECK(f.stats.first.pfifo == 5 && f.stats.first.pgraph == 0x80);
    enable(false, false, 3); enable(true, true, 1);
    CHECK(!f.stats.first.fifo_latched && !f.stats.first.graph_latched);
    aggregate_hidden = false;
    reg_at(0x2100)->value = 2; reg_at(0x400100)->value = 0x10;
    reg_at(0x400704)->value = 0x50108; reg_at(0x400708)->value = 0xfeed;
    CHECK(irq() == IRQ_HANDLED);
    CHECK(f.stats.first.pfifo == 2 && f.stats.first.pgraph == 0x10);
    CHECK(f.stats.first.gr_method == 0x50108 && f.stats.first.gr_data == 0xfeed);
    fault_off = 0x400108; poll(); CHECK(f.last_rc == GF1_EIO && !reg_at(0x140)->value);
}
static void vblank(void)
{
    clear_state(); enable(false, false, 1);
    reg_at(0x600100)->value = reg_at(0x602100)->value = 1;
    CHECK(irq() == IRQ_HANDLED);
    CHECK(f.stats.vblank[0] == 1 && !f.stats.vblank[1]);
    CHECK(!reg_at(0x600100)->value && !reg_at(0x602100)->value && ack_head[0] == 1 && ack_head[1] == 1);
    f.stats.vblank[0] = ~0U; reg_at(0x600100)->value = 1;
    CHECK(irq() == IRQ_HANDLED && !f.stats.vblank[0]);
    enable(false, false, 3);
    reg_at(0x600100)->value = reg_at(0x602100)->value = 1;
    CHECK(irq() == IRQ_HANDLED && f.stats.vblank[0] == 1 && f.stats.vblank[1] == 1);
    enable(false, false, 0);
    reg_at(0x600100)->value = reg_at(0x602100)->value = 1;
    poll(); CHECK(f.stats.vblank[0] == 1 && f.stats.vblank[1] == 1);
    CHECK(!reg_at(0x600100)->value && !reg_at(0x602100)->value);
    /* Old pending is cleared before re-enable, without counting it. */
    reg_at(0x600100)->value = 1; enable(false, false, 1);
    CHECK(irq() == IRQ_NONE && f.stats.vblank[0] == 1);
    struct gf_irq_stats snapshot;
    for (u32 flags = 2; flags <= 0x202; flags += 0x200) {
        host_if = flags; gf_irq_snapshot(&f, &snapshot);
        CHECK(host_if == flags && snapshot.vblank[0] == 1 && snapshot.vblank[1] == 1);
        CHECK(snapshot.entries == f.stats.entries && snapshot.pmc == f.stats.pmc);
    }
    CHECK(saves == restores);
}
static void deferred(void)
{
    clear_state(); enable(true, true, 1);
    reg_at(0x2100)->value = reg_at(0x2100)->sticky = 4;
    reg_at(0x400100)->value = reg_at(0x400100)->sticky = 8;
    reg_at(0x600100)->value = reg_at(0x600100)->sticky = 1;
    CHECK(irq() == IRQ_DEFERRED && !reg_at(0x140)->value);
    CHECK(!(reg_at(0x2140)->value & 4) && !(reg_at(0x40013c)->value & 8) && !reg_at(0x600140)->value);
    poll(); CHECK(!reg_at(0x140)->value);
    reg_at(0x2100)->sticky = reg_at(0x400100)->sticky = reg_at(0x600100)->sticky = 0;
    poll(); CHECK(reg_at(0x140)->value == 1 && reg_at(0x2140)->value == ~0U);
    CHECK(reg_at(0x40013c)->value == ~0U && reg_at(0x600140)->value == 1);
    unknown = 0x80000000U; reg_at(0x600100)->value = 1;
    CHECK(irq() == IRQ_DEFERRED && !reg_at(0x140)->value);
    u32 count = f.stats.vblank[0];
    reg_at(0x600100)->value = 1; poll();
    CHECK(f.stats.vblank[0] == count + 1 && !reg_at(0x140)->value && (f.stats.unknown & unknown));
    unknown = 0; poll(); CHECK(reg_at(0x140)->value == 1);
    /* PTIMER/PVIDEO residual then recovery; unused sources never re-enabled. */
    reg_at(0x8100)->value = reg_at(0x8100)->sticky = 2;
    reg_at(0x8140)->value = 2;
    CHECK(irq() == IRQ_DEFERRED);
    reg_at(0x9100)->value = reg_at(0x9100)->sticky = 4;
    poll(); CHECK(!reg_at(0x140)->value && !reg_at(0x8140)->value && !reg_at(0x9140)->value);
    reg_at(0x8100)->sticky = reg_at(0x9100)->sticky = 0;
    reg_at(0x1100)->value = 0x11; reg_at(0xb100)->value = 1;
    poll(); CHECK(reg_at(0x140)->value == 1);
    CHECK(!reg_at(0x8100)->value && !reg_at(0x9100)->value && !reg_at(0x1100)->value && !reg_at(0xb100)->value);
    f.running = false; live = false;
    u32 before = ops; CHECK(irq() == IRQ_NONE); poll(); CHECK(ops == before);
}
void _start(void)
{
    registration(); dispatch(); vblank(); deferred();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
