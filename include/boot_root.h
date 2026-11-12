#ifndef BOOT_ROOT_H
#define BOOT_ROOT_H

#include "types.h"

struct boot_root_image { const u8 *base; u32 bytes; };
/* The platform reserves and maps base; the reservation, mapping and contents
 * stay for the whole system lifetime after mount and are never freed.
 * The caller has initialized VFS and registered its device name table and
 * the filesystem (iso9660).
 * The boot medium is not read after boot. */
int boot_root_mount(const struct boot_root_image *image);

/* Register ISO9660 once after VFS initialization. */
int boot_root_fs_init(void);
/* ISO9660 and device names must already be registered. */
int boot_root_start(const struct boot_root_image *image);

#endif
