#include "exec_font_lease.h"
#include "os32_kapi_shared.h"
i32 exec_font_lease_shell_open(const char *path, u32 gen, u32 *id, u32 *size) { (void)path; (void)gen; (void)id; (void)size; return OS32_ERR_NOTFOUND; }
i32 exec_font_lease_shell_read_at(u32 id, u32 gen, u32 offset, void *dst, u32 len) { (void)id; (void)gen; (void)offset; (void)dst; (void)len; return OS32_ERR_NOTFOUND; }
i32 exec_font_lease_shell_validate(u32 id, u32 gen) { (void)id; (void)gen; return OS32_ERR_NOTFOUND; }
i32 exec_font_lease_shell_close(u32 id, u32 gen) { (void)id; (void)gen; return 0; }
void exec_font_lease_close_shell(void) { }
void exec_font_lease_close_owned(int owner) { (void)owner; }
