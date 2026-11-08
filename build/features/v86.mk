# feature/v86 owns this selection.
C_FEATURES += arch/x86/legacy/v86.c arch/x86/legacy/v86_mem.c arch/x86/legacy/v86_module.c arch/x86/legacy/v86_gcap.c arch/x86/legacy/v86_call.c arch/x86/legacy/guest.c arch/x86/legacy/lowmap.c
ASM_FEATURES += arch/x86/legacy/v86_entry.asm
INCLUDE_FEATURES += arch/x86/legacy platform/pc98/legacy
MODULE_FEATURES += v86
MODULE_MK_v86 = build/v86-module.mk
