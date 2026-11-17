#include "exec_shlib.h"
int exec_shlib_addrspace_attach(struct addrspace *as) { (void)as; return 0; }
void exec_shlib_addrspace_detach(struct addrspace *as) { (void)as; }
int exec_shlib_loaded(void) { return 0; }
u32 exec_shlib_data_pages(void) { return 0; }
int exec_shlib_read_page(u32 va, u32 frame) { (void)va; (void)frame; return 0; }
