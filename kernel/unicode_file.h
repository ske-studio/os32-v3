/* Private kernel conversion; file layout shared only with the host generator. */
#ifndef UNICODE_FILE_H
#define UNICODE_FILE_H

#include "types.h"

#define UNICODE_FILE_PAIRS       7063
#define UNICODE_FILE_PAIR_BYTES  4
#define UNICODE_FILE_BYTES      (UNICODE_FILE_PAIRS * UNICODE_FILE_PAIR_BYTES)
#define UNICODE_FILE_PAGES      7

/* Call once after root mount, paging and the initial memory selftests.
 * Success retains kernel-owned pages until shutdown; failure leaves kana only.
 */
void unicode_file_load(void);
u16 unicode_file_lookup(u32 cp);
/* kdebug only; not a KAPI or user-visible table pointer. */
u32 unicode_file_debug_phys(void);

#endif
