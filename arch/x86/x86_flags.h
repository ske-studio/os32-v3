/* x86 EFLAGS bits shared by cpu.h, arch_io.h and preprocessed assembly.
 * Keep this header independent of the MMU/GDT/TSS C implementation. */
#ifndef X86_FLAGS_H
#define X86_FLAGS_H

#define EFLAGS_TF 0x000100
#define EFLAGS_IF 0x000200
#define EFLAGS_VM 0x020000
#define EFLAGS_RESERVED1 0x000002

#endif /* X86_FLAGS_H */
