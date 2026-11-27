#include <stdio.h>
#include <stdlib.h>
#include "opn_probe.h"
#include "platform_fm_probe.h"

volatile u32 kctx_irq_depth;
/* Future shared-line configuration, within the production table capacity. */
const struct irq_topology *platform_irq_topology(void)
{
    static const unsigned lines[] = {3, 10, 12, 13};
    static const struct irq_topology topology = {16, 4, lines};
    return &topology;
}
static int sound_irq(unsigned irq, void *arg)
{
    (void)irq;
    (void)arg;
    return IRQ_NONE;
}

static unsigned statuses[2], indices[2], ids[2], feature, writes, reads;
static int ticks, fifo_response, restore_failure;
unsigned inp(unsigned port)
{
    ++reads;
    unsigned bank = port >= 0x288;
    if (port == 0xa460) return feature;
    if (port == 0xa468) return fifo_response;
    if (port == 0x188 || port == 0x288) return statuses[bank];
    if ((port == 0x18a || port == 0x28a) && indices[bank] == 0xff)
        return ids[bank];
    return 0xff;
}
void outp(unsigned port, unsigned value)
{
    ++writes;
    unsigned bank = port >= 0x288;
    if (port == 0x188 || port == 0x288) indices[bank] = value;
    if (port == 0x18a && indices[bank] == 0x29 && value == 3 && restore_failure)
        statuses[bank] = 0x80;
    if ((port == 0x18a || port == 0x28a) && indices[bank] == 0x27)
        statuses[bank] = value == 0x0a && ticks ? 2 : 0;
}
static void require(int ok, const char *why)
{
    if (!ok) { puts(why); exit(1); }
}
int main(void)
{
    struct fm_probe_record r = {0};
    feature = 0xff;
    statuses[0] = statuses[1] = 0xff;
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[0].model == OPN_PROBE_ABSENT && r.bank[1].model == OPN_PROBE_ABSENT, "open bus");
    statuses[0] = 0;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[0].model == OPN_PROBE_UNKNOWN, "missing timer transition");
    ticks = 1;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[0].model == OPN_PROBE_OPN, "26K");
    restore_failure = 1;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 3);
    require(!r.contained && r.bank[0].model == OPN_PROBE_UNKNOWN, "restore timeout");
    restore_failure = 0;
    statuses[0] = 4;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[0].model == OPN_PROBE_UNKNOWN && r.bank[1].model == OPN_PROBE_ABSENT, "unknown first");
    statuses[0] = 0xff;
    feature = 0x52;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[1].model == OPN_PROBE_UNKNOWN, "masked alternate");
    statuses[1] = 0;
    ids[1] = 1;
    fifo_response = 0xff;
    feature = 0x51;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(!r.fifo_port, "OPNA without FIFO");
    fifo_response = 0;
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(r.bank[1].model == OPN_PROBE_OPNA && r.bank[1].chip_id_port == 0x28a && r.fifo_port == 0xa468, "alternate OPNA");
    require(r.feature_id == feature && r.feature_port == 0xa460 && r.bank[1].chip_id == 1, "verified IDs");
    require(irq_register(12, sound_irq, NULL, IRQ_F_SHARED) == 0, "register IRQ12");
    reads = writes = 0;
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(!reads && !writes && r.bank[1].model == OPN_PROBE_OPNA, "reattach");
    r = (struct fm_probe_record){0};
    opn_probe_once(&r, platform_fm_probe_busy(), 0);
    require(!reads && !writes && r.bank[0].model == OPN_PROBE_UNKNOWN && r.bank[1].model == OPN_PROBE_UNKNOWN, "registered IRQ");
}
