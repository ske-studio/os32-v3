/* Shared transport for buffered calls and the legacy streaming API. */
#ifndef HOST_CALL_INTERNAL_H
#define HOST_CALL_INTERNAL_H
#include "libos32host.h"
typedef int (*host_body_fn)(void *ctx, const u8 *buf, u32 n);
int host_exchange(const char *req, u32 req_len, const void *in, u32 in_len,
                  host_body_fn body, void *ctx, int *status, u32 timeout);
#endif
