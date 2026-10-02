/* Legacy SQLite-engine tests model the caller boundary only. The real B1
 * wrapper -> copy -> walk path is exercised by db_caller_host.c. */
#include "redir_access.h"
int ring3_user_range_ok(u32 p, u32 len);
int redir_access_capture(RedirAccess *out) { (void)out; return 1; }
int copy_caller_cstr(const struct caller_access *c, const char *src, char *dst, u32 cap)
{
    (void)c;
    if (!src || !dst) return 0;
    for (u32 i = 0; i < cap; i++) {
        if (!ring3_user_range_ok((u32)(unsigned long)(src + i), 1)) return 0;
        dst[i] = src[i];
        if (!dst[i]) return 1;
    }
    return 0;
}
int copy_caller_bytes(const struct caller_access *c, const void *src, void *dst, u32 len)
{
    (void)c;
    if (!src || (len && len - 1 > 0xffffffffUL - (u32)(unsigned long)src) || !ring3_user_range_ok((u32)(unsigned long)src, len)) return 0;
    for (u32 i = 0; i < len; i++) ((char *)dst)[i] = ((const char *)src)[i];
    return 1;
}
