# feature/v86 owns this selection.
C_FEATURES += kernel/v86/v86.c kernel/v86/v86_mem.c kernel/v86/v86_snapshot.c kernel/v86/v86_module.c kernel/v86/v86_gcap.c arch/x86/legacy/v86_call.c arch/x86/legacy/guest.c arch/x86/legacy/lowmap.c
ASM_FEATURES += kernel/v86/v86_entry.asm
INCLUDE_FEATURES += kernel/v86
MODULE_FEATURES += v86
MODULE_MK_v86 = build/v86-module.mk
