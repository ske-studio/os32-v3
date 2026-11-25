/* PC-9821 resident NIC match wiring; callbacks never point into lan.mod. */
#include "lan_module.h"
#include "e82557_regs.h"

const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", lan_module_probe
};

const struct pci_driver *platform_lan_driver(void)
{
    return &e82557_driver;
}
