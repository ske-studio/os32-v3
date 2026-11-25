#ifndef PCI_BARSIZE_H
#define PCI_BARSIZE_H
#include "types.h"
struct pci_bar_resource {
    unsigned long long base, len;
    u32 raw, high, kind, prefetch, unassigned;
};
#endif
