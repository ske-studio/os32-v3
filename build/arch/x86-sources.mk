# arch/x86 owns this selection.
C_ARCH += arch/x86/kernel/boot_cpu.c arch/x86/kernel/boot_after_paging.c arch/x86/kernel/fault_fixture.c arch/x86/kernel/mtrr_wc.c kernel/kstack_hw.c arch/x86/kernel/gdt.c arch/x86/kernel/double_fault.c arch/x86/kernel/tss.c arch/x86/kernel/idt.c arch/x86/kernel/isr_handlers.c kernel/time/cpu_calibrate.c kernel/time/sysclk.c exec/ring3_ls.c
ASM_ARCH += arch/x86/kernel/kentry.asm arch/x86/kernel/isr_stub.asm arch/x86/kernel/ring3_entry.asm arch/x86/kernel/setjmp.asm lib/kstring_asm.asm
INCLUDE_ARCH += arch/x86/kernel
ASM_EXTRA_OBJ = arch/x86/kernel/ring3_ls_shim.o
KSTRING_C_SRC =

C_ARCH += arch/x86/kernel/exec_entry.c
C_ARCH += arch/x86/kernel/exec_context.c
C_ARCH_NEW += arch/x86/kernel/exec_entry.c
C_ARCH_NEW += arch/x86/kernel/exec_context.c

C_ARCH += arch/x86/kernel/kapi_call.c arch/x86/kernel/syscall_entry.c
C_ARCH_NEW += arch/x86/kernel/kapi_call.c arch/x86/kernel/syscall_entry.c
