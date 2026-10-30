#ifndef KAPI_RANGE_INTERNAL_H
#define KAPI_RANGE_INTERNAL_H

#include "exec.h"               /* ring3_user_range_ok */

/* 長さ付きユーザポインタの検証。 */
static int kapi_user_range_ok(const void *p, u32 len)
{
    u32 a = (u32)p;
    if (!p) return 0;
    if (a + len < a) return 0;              /* 加算 overflow */
    return ring3_user_range_ok(a, len);
}

#endif
