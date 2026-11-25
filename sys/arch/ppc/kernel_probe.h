#ifndef OS32_PPC_KERNEL_PROBE_H
#define OS32_PPC_KERNEL_PROBE_H
#include "types.h"
int ppc_probe_read(u32 va);
int ppc_probe_copy8(u32 va, u8 *out);
int ppc_probe_exec(u32 target);
void ppc_probe_syscall(void);
extern const char ppc_probe_copy8_insn[], ppc_probe_copy8_resume[];
extern const char ppc_probe_read_insn[], ppc_probe_read_resume[];
extern const char ppc_probe_exec_resume[];
#endif
