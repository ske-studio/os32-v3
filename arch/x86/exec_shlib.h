#ifndef OS32_EXEC_SHLIB_H
#define OS32_EXEC_SHLIB_H
#include "shlib.h"
static inline int exec_shlib_addrspace_attach(struct addrspace *as) { return shlib_addrspace_attach(as); }
static inline void exec_shlib_addrspace_detach(struct addrspace *as) { shlib_addrspace_detach(as); }
static inline int exec_shlib_loaded(void) { return shlib_loaded(); }
static inline u32 exec_shlib_data_pages(void) { return shlib_data_pages(); }
static inline int exec_shlib_read_page(u32 va, u32 frame) { return shlib_read_page(va, frame); }
#endif
