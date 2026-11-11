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

#endif
