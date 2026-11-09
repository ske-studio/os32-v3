/* Syntax-only probe; shell_pci_record.h is extracted from cmd_pci.c by the test.
 * Public records carry target-native numbers, unlike the PC98 LE boot wire.
 * This file is never included by a product translation unit. */
#include "os32_gui_shared.h"
#include "pci_dev_contract.h"
#include "shell_pci_record.h"

#define RECORD_FIELD(kernel, shell) \
    (__builtin_offsetof(struct pci_dev, kernel) == __builtin_offsetof(PciDev, shell))
#define RECORD_LAYOUT_MATCHES (sizeof(struct pci_dev) == sizeof(PciDev) && \
    RECORD_FIELD(bus, bus) && RECORD_FIELD(dev, dev) && RECORD_FIELD(fn, fn) && \
    RECORD_FIELD(vendor, vendor) && RECORD_FIELD(device, device) && \
    RECORD_FIELD(class, cls) && RECORD_FIELD(subclass, subclass) && \
    RECORD_FIELD(progif, progif) && RECORD_FIELD(header, header) && \
    RECORD_FIELD(bar, bar) && RECORD_FIELD(irq_line, irq_line) && \
    RECORD_FIELD(irq_pin, irq_pin) && RECORD_FIELD(command, command))

/* The host32 oracle evaluates this same expression at runtime so a changed
 * shell layout must compile successfully before it can count as a RED. */
#ifndef RECORD_LAYOUT_RUNTIME
STATIC_ASSERT(RECORD_LAYOUT_MATCHES, pci_shell_record_matches);
#endif
