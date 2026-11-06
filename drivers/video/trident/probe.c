/* Identification follows trident_driver-1.4.0.c:2294,2313,2384,2525-2529
 * and userland/tests/hw/trident_mode.hwb:61-81. The MIT/XFree86
 * and Alan Hourihane notices are retained in trident.h. */
#include "trident.h"

int trident_pci_validate(const struct trident_pci_info *pci)
{
    if (!pci) return TRI_E_INVAL;
    if (pci->bus != TRI_PCI_BUS || pci->slot != TRI_PCI_SLOT ||
        pci->function != TRI_PCI_FUNCTION || pci->vendor != TRI_PCI_VENDOR ||
        pci->device != TRI_PCI_DEVICE || !(pci->command & TRI_PCI_COMMAND))
        return TRI_E_DEVICE;
    /* These fixed windows are the approved Ra266 decode bounds. */
    if (pci->bar0 != TRI_BAR0_PHYS || pci->bar1 != TRI_BAR1_PHYS)
        return TRI_E_RESOURCE;
    return TRI_OK;
}

int trident_identify(struct trident_device *d)
{
    u8 chip;
    int rc;
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (!d->pinned || !d->armed || !d->res->ready) return TRI_E_STATE;
    rc = trident_pci_validate(&d->res->pci);
    if (rc) return rc;
    rc = trident_indexed_read(d, TRI_SR_INDEX, 0x09, &chip);
    if (rc) return rc;
    if (chip != TRI_CHIP_9682) return TRI_E_DEVICE;
    d->chip = chip;
    return TRI_OK;
}
