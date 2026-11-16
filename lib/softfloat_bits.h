/* Internal integer-only boundary; not a public SDK or native double ABI. */
#ifndef OS32_SOFTFLOAT_BITS_H
#define OS32_SOFTFLOAT_BITS_H
#include "types.h"

/* IEEE binary64 bits, EDX:EAX on i386. Truncate toward zero; nonfinite or
 * out of signed i32 range returns INT32_MIN (also a valid in-range result).
 * B2 saves/restores all four SoftFloat modes; callers hold IRQs, no reentry. */
i32 softfp_f64_to_i32_trunc(unsigned long long bits);

#endif
