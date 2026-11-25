# arch/x86 owns this selection.
C_ARCH += sys/arch/x86/kernel/boot_cpu.c sys/arch/x86/kernel/boot_after_paging.c sys/arch/x86/kernel/fault_fixture.c sys/arch/x86/kernel/mtrr_wc.c sys/arch/x86/kernel/video_wc.c sys/kern/kstack_hw.c sys/arch/x86/kernel/gdt.c sys/arch/x86/kernel/double_fault.c sys/arch/x86/kernel/tss.c sys/arch/x86/kernel/idt.c sys/arch/x86/kernel/isr_handlers.c sys/platform/pc98/time/cpu_calibrate.c sys/platform/pc98/sysclk.c
ASM_ARCH += sys/arch/x86/kernel/kentry.asm sys/arch/x86/kernel/isr_stub.asm sys/arch/x86/kernel/ring3_entry.asm sys/arch/x86/kernel/setjmp.asm sys/kern/kstring_asm.asm
INCLUDE_ARCH += sys/arch/x86/kernel
ASM_EXTRA_OBJ = arch/x86/kernel/ring3_ls_shim.o
KSTRING_C_SRC =

C_ARCH += sys/arch/x86/kernel/exec_entry.c
C_ARCH += sys/arch/x86/kernel/exec_context.c
C_ARCH_NEW += sys/arch/x86/kernel/exec_entry.c
C_ARCH_NEW += sys/arch/x86/kernel/exec_context.c

C_ARCH += sys/arch/x86/kernel/kapi_call.c sys/arch/x86/kernel/syscall_entry.c
C_ARCH_NEW += sys/arch/x86/kernel/kapi_call.c sys/arch/x86/kernel/syscall_entry.c

C_ARCH += sys/arch/x86/kernel/syscall_code.c
