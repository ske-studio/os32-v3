# feature/v86 owns this selection.
C_FEATURES += sys/platform/pc98/legacy/v86.c sys/platform/pc98/legacy/v86_mem.c sys/platform/pc98/legacy/v86_module.c sys/platform/pc98/legacy/v86_gcap.c sys/platform/pc98/legacy/v86_call.c sys/platform/pc98/legacy/guest.c sys/platform/pc98/legacy/lowmap.c
ASM_FEATURES += sys/platform/pc98/legacy/v86_entry.asm
INCLUDE_FEATURES += sys/platform/pc98/legacy
MODULE_FEATURES += v86
MODULE_MK_v86 = build/v86-module.mk
