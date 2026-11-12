#ifndef OS32_ARCH_EXEC_IMAGE_H
#define OS32_ARCH_EXEC_IMAGE_H
#include "types.h"
#include "os32_kapi_shared.h"
/* PowerMac accepts only images carrying the PPC32 bit (ppc32-sysv-soft-v1). */
static inline int arch_exec_image_accept(u32 flags)
{
    return (flags & OS32X_FLAG_PPC32) != 0;
}
#endif
