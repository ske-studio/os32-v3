#ifndef OS32_EXEC_FONT_LEASE_H
#define OS32_EXEC_FONT_LEASE_H
#include "types.h"
i32 exec_font_lease_shell_open(const char *path, u32 gen, u32 *id, u32 *size);
i32 exec_font_lease_shell_read_at(u32 id, u32 gen, u32 offset, void *dst, u32 len);
i32 exec_font_lease_shell_validate(u32 id, u32 gen);
i32 exec_font_lease_shell_close(u32 id, u32 gen);
void exec_font_lease_close_shell(void);
void exec_font_lease_close_owned(int owner);
#endif
