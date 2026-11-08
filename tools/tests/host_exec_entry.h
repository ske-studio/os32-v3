/* ILP32 fixtures compile the product entry body; only final ASM is observed. */
#ifndef OS32_HOST_EXEC_ENTRY_H
#define OS32_HOST_EXEC_ENTRY_H
#include "cpu.h"
#ifndef ARCH_ENTRY_USER
#define ARCH_ENTRY_USER(pd, sp, ip) do { (void)(pd); (void)(sp); (void)(ip); for (;;) {} } while (0)
#endif
#ifndef ARCH_ENTRY_CALL
#define ARCH_ENTRY_CALL(saved, sp, ip) do { (void)(saved); (void)(sp); (void)(ip); for (;;) {} } while (0)
#endif
#ifdef HOST_EXEC_ENTRY_SOURCE
#include HOST_EXEC_ENTRY_SOURCE
#else
#include "../../arch/x86/kernel/exec_entry.c"
#endif
#endif
