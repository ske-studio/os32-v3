# Existing bootstrap units, compiled by the selected kernel graph.
KSTRING_C_SRC = lib/kstring_c.c
ifeq ($(IMAGE_ORDER),kboot)
# kboot の像の持ち物 (像の機能ごと)。
GAS_ARCH_CORE = arch/ppc/kernel_entry.S arch/ppc/entry.S arch/ppc/kernel_takeover.S arch/ppc/kernel_vectors.S arch/ppc/kernel_mmu.S arch/ppc/kernel_probe.S arch/ppc/mmu.S arch/ppc/cache_copy.S arch/ppc/fp_context.S
GAS_ARCH_EXEC = arch/ppc/exec_resume.S arch/ppc/exec_enter.S arch/ppc/setjmp.S
C_ARCH_CORE = arch/ppc/kernel_boot.c arch/ppc/kernel_cpu.c arch/ppc/kernel_hpt.c arch/ppc/kernel_trap.c arch/ppc/kernel_dec.c arch/ppc/pd_walk.c arch/ppc/dma_cache.c arch/ppc/kernel_fp.c
C_ARCH_EXEC = $(wildcard arch/ppc/exec_*.c) arch/ppc/syscall_code.c arch/ppc/kapi_call.c kernel/time/cpu_calibrate.c
C_ARCH_NOEXEC = arch/ppc/kboot_unavailable.c
else
# compile だけの計測の集合 (像 ppc-selected)。計測と一緒に消す。
GAS_KERNEL = arch/ppc/entry.S arch/ppc/ofw_note.S

C_ARCH += arch/ppc/video_wc.c
# Compile-only GPU applicability; no PPC display backend is provided.
C_ARCH += drivers/video/geforce/gf_wc.c
endif
