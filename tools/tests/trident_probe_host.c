#include "trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 storage[TRI_FAKE_REAL_BYTES];
static struct trident_fake_event trace[8];
int trident_indexed_read(struct trident_device *dev, u16 port, u8 index, u8 *out)
{
    int rc = dev->io->write8(dev->io_opaque, TRI_SPACE_REGS, port, index);
    if (rc) return rc;
    return dev->io->read8(dev->io_opaque, TRI_SPACE_REGS, port + 1, out);
}
static int test(void)
{
    if (trident_fake_reset(&f, storage, sizeof(storage), trace, 8)) return 1;
    trident_fake_bind(&f, &d);
    if (trident_pci_validate(&f.resources.pci)) return 2;
    if (trident_identify(&d) != TRI_E_STATE || f.operations) return 3;
    d.pinned = d.armed = f.resources.ready = true;
    d.reference = 0x80;
    if (trident_identify(&d) || d.chip != 0x10 || d.reference != 0x80) return 4;
    if (f.trace_count != 2 || trace[0].offset != TRI_SR_INDEX ||
        trace[0].value != 9 || trace[1].offset != TRI_SR_INDEX + 1 ||
        f.writes[0] != 1 || f.reads[0] != 1 || f.writes[1] ||
        f.reads[1] || f.writes[2] || f.reads[2]) return 5;
    f.sr[9] = 1;
    if (trident_identify(&d) != TRI_E_DEVICE) return 6;
    f.sr[9] = 0x10;
    trident_fake_fail(&f, f.operations + 2, -99);
    if (trident_identify(&d) != -99) return 7;
    f.resources.pci.bar1 |= 1;
    if (trident_pci_validate(&f.resources.pci) != TRI_E_RESOURCE) return 8;
    f.resources.pci.bar1 &= ~1u;
    f.resources.pci.vendor = 0;
    if (trident_pci_validate(&f.resources.pci) != TRI_E_DEVICE) return 9;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
