#ifndef OS32_EXEC_KAPI_H
#define OS32_EXEC_KAPI_H
#include "types.h"
#include "memmap.h"
static inline const u32 *exec_kapi_table(void) { return (const u32 *)KAPI_ADDR; }
#endif
