#ifndef OS32_EXEC_FONT_LEASE_H
#define OS32_EXEC_FONT_LEASE_H
#include "exec_hooks.h"
#if OS32_HOOK_FONT_LEASE
#include "font_source.h"
static inline i32 exec_font_lease_shell_open(const char *path, u32 gen, u32 *id, u32 *size) { return font_lease_shell_open(path, gen, id, size); }
static inline i32 exec_font_lease_shell_read_at(u32 id, u32 gen, u32 offset, void *dst, u32 len) { return font_lease_shell_read_at(id, gen, offset, dst, len); }
static inline i32 exec_font_lease_shell_validate(u32 id, u32 gen) { return font_lease_shell_validate(id, gen); }
static inline i32 exec_font_lease_shell_close(u32 id, u32 gen) { return font_lease_shell_close(id, gen); }
static inline void exec_font_lease_close_shell(void) { font_lease_close_shell(); }
static inline void exec_font_lease_close_owned(int owner) { font_lease_close_owned(owner); }
#else
#include "types.h"
i32 exec_font_lease_shell_open(const char *path, u32 gen, u32 *id, u32 *size);
i32 exec_font_lease_shell_read_at(u32 id, u32 gen, u32 offset, void *dst, u32 len);
i32 exec_font_lease_shell_validate(u32 id, u32 gen);
i32 exec_font_lease_shell_close(u32 id, u32 gen);
void exec_font_lease_close_shell(void);
void exec_font_lease_close_owned(int owner);
#endif
#endif
