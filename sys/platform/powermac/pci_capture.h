#ifndef PM_PCI_CAPTURE_H
#define PM_PCI_CAPTURE_H
#include "types.h"

/* PCI Bus Binding to IEEE 1275 §2.2.1; NetBSD macppc/pci/uninorth.c.
 * Parent indices refer to immutable preorder records; -1 denotes the CPU. */
#define PM_PCI_BRIDGES 24
#define PM_PCI_FUNCTIONS 96
#define PM_PCI_RANGES 128
#define PM_PCI_ASSIGNED 256
struct pm_of_bridge {
    int id, parent, host;
    unsigned ac, sc, parent_ac, parent_sc;
    u32 first, last, cfg_pa;
    unsigned incomplete, supported;
};
struct pm_of_range {
    int bridge;
    unsigned space, parent_space;
    unsigned long long base, parent_base, bytes;
};
struct pm_of_function {
    int id, bridge;
    u32 vendor, device, class_code;
    unsigned compatible_bytes;
    char compatible[128];
};
struct pm_of_assignment {
    int function;
    u32 hi;
    unsigned long long base, bytes;
};
struct pm_pci_capture {
    unsigned bridges, functions, ranges, assigned, incomplete, gmac_known;
    struct pm_of_bridge bridge[PM_PCI_BRIDGES];
    struct pm_of_range range[PM_PCI_RANGES];
    struct pm_of_function function[PM_PCI_FUNCTIONS];
    struct pm_of_assignment assignment[PM_PCI_ASSIGNED];
};
struct pm_bridge_state {
    int host_id;
    u32 addr_va, data_va;
    unsigned busy, isolated;
};
struct pm_bridge_state *pm_bridge_register(int host_id, u32 addr_va, u32 data_va);
struct pm_bridge_state *pm_bridge_lookup(int host_id);
#endif
