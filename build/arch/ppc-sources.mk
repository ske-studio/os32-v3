# Existing bootstrap units, compiled by the selected kernel graph.
KSTRING_C_SRC = sys/kern/kstring_c.c
# kboot の像の持ち物 (像の機能ごと)。
GAS_ARCH_CORE = sys/arch/ppc/kernel_entry.S sys/arch/ppc/entry.S sys/arch/ppc/kernel_takeover.S sys/arch/ppc/kernel_vectors.S sys/arch/ppc/kernel_mmu.S sys/arch/ppc/kernel_probe.S sys/arch/ppc/hwb_transfer.S sys/arch/ppc/mmu.S sys/arch/ppc/cache_copy.S sys/arch/ppc/fp_context.S
GAS_ARCH_EXEC = sys/arch/ppc/exec_resume.S sys/arch/ppc/exec_enter.S sys/arch/ppc/setjmp.S
C_ARCH_CORE = sys/platform/powermac/kernel_boot.c sys/arch/ppc/kernel_cpu.c sys/arch/ppc/kernel_hpt.c sys/arch/ppc/kernel_trap.c sys/arch/ppc/kernel_dec.c sys/arch/ppc/pd_walk.c sys/arch/ppc/dma_cache.c sys/arch/ppc/kernel_fp.c
C_ARCH_EXEC = sys/arch/ppc/user_fault.c sys/arch/ppc/exec_cache.c sys/arch/ppc/exec_context.c sys/arch/ppc/exec_entry.c sys/arch/ppc/exec_kapi.c sys/arch/ppc/exec_selftest.c sys/arch/ppc/syscall_code.c sys/arch/ppc/kapi_call.c sys/platform/pc98/time/cpu_calibrate.c
C_ARCH_NOEXEC = sys/arch/ppc/kboot_unavailable.c
