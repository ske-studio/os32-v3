#ifndef RAM_RO_H
#define RAM_RO_H

#include "drivers/base/dev.h"
#include "boot_root.h"

struct ram_ro { Device dev; const u8 *base; u32 bytes; };
/* No name argument: register rd0 through dev_register, with DEV_BUS_NONE.
 * PPC does not call dev_init; its device table starts in BSS. */
int ram_ro_register(struct ram_ro *, const struct boot_root_image *);
int ram_ro_read(struct ram_ro *, u32 lba, u32 count, void *dst);

#endif
