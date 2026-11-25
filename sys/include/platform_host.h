#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H

#include "types.h"

/* Synchronous transport only: link owns handles, owner state and buffers.
 * KAPI validates and copies user data; platform never allocates or caches it.
 * read_stage borrows link's const buffer: copy before the next operation.
 * Owner cleanup retains link's close order and the caller's owner number.
 * Disconnected/error results pass through unchanged; no detection here. */
i32 platform_host_open(const char *req, u32 len, int owner);
i32 platform_host_status(i32 h, u32 *status, u32 *length, int owner);
i32 platform_host_read_stage(i32 h, u32 cap, const u8 **out, int owner);
i32 platform_host_write(i32 h, const void *buf, u32 len, int owner);
i32 platform_host_close(i32 h, int owner);
void platform_host_owner_exit(int owner);

#endif /* PLATFORM_HOST_H */
