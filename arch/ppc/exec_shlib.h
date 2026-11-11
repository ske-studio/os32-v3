#ifndef OS32_EXEC_SHLIB_H
#define OS32_EXEC_SHLIB_H
#include "types.h"
struct addrspace;
int exec_shlib_addrspace_attach(struct addrspace *as);
void exec_shlib_addrspace_detach(struct addrspace *as);
int exec_shlib_loaded(void);
u32 exec_shlib_data_pages(void);
int exec_shlib_read_page(u32 va, u32 frame);
#endif
