# feature/v86 owns this selection.
C_FEATURES += platform/pc98/legacy/v86.c platform/pc98/legacy/v86_mem.c platform/pc98/legacy/v86_module.c platform/pc98/legacy/v86_gcap.c platform/pc98/legacy/v86_call.c platform/pc98/legacy/guest.c platform/pc98/legacy/lowmap.c
ASM_FEATURES += platform/pc98/legacy/v86_entry.asm
INCLUDE_FEATURES += platform/pc98/legacy
MODULE_FEATURES += v86
MODULE_MK_v86 = build/v86-module.mk
