#ifndef OS32_ARCH_EXEC_IMAGE_H
#define OS32_ARCH_EXEC_IMAGE_H
#include "types.h"
#include "os32_kapi_shared.h"
/* x86 accepts only images without the PPC32 bit.  A macro keeps os32x_hdr.o
 * byte-identical (an inline function renames GCC's local symbols). */
#define arch_exec_image_accept(flags) (((flags) & OS32X_FLAG_PPC32) == 0)
#endif
