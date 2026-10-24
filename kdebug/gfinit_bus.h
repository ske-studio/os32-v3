#ifndef KDEBUG_GFINIT_BUS_H
#define KDEBUG_GFINIT_BUS_H
#include "kdebug_window.h"
/* S6 supplies existing pci_cfg_* imports. Command always uses write16;
 * no config DWORD read/modify/write, no GPU legacy I/O instructions. */
struct gfinit_pci_ops {
    u32 (*read32)(u32 bus, u32 dev, u32 fn, u32 reg);
    u16 (*read16)(u32 bus, u32 dev, u32 fn, u32 reg);
    void (*write32)(u32 bus, u32 dev, u32 fn, u32 reg, u32 value);
    int (*write16)(u32 bus, u32 dev, u32 fn, u32 reg, u16 value);
};
struct gfinit_bus {
    const struct gfinit_pci_ops *pci;
    const struct recover_profile *profile;
    struct recover_window window;
    struct recover_identity identity;
    u32 bars[RECOVER_BAR_COUNT];
    bool ready;
};
void gfinit_bus_init(struct gfinit_bus *b, const struct recover_profile *profile,
                     const struct gfinit_pci_ops *pci);
/* Read-only PCI probe; identity boot/image/kdebug fields are supplied by S6. */
int gfinit_bus_probe(struct gfinit_bus *b);
int gfinit_bus_identity(void *opaque, struct recover_identity *out);
int gfinit_bus_raw_config(void *opaque, u32 *command, u32 bars[RECOVER_BAR_COUNT]);
/* recover_ops accessors, only after recover has authorized the resolved tuple. */
int gfinit_bus_read(void *opaque, const struct recover_bus_access *a, u32 *value);
int gfinit_bus_write(void *opaque, const struct recover_bus_access *a, u32 value);
int gfinit_bus_local_cleanup(struct gfinit_bus *b, struct recover *r);
#endif
