# feature/sqlite owns this selection.
C_FEATURES += kernel/sqlite_engine.c kernel/sqlite_outer.c
ifeq ($(ARCH),ppc)
GAS_ARCH_EXEC += arch/ppc/sqlite_stack_ppc.S
include build/features/sqlite-static.mk
else
C_FEATURES += kernel/ime/fep_boot_config.c
C_FEATURES += lib/sqlite3/os32_sqlite_test.c
ASM_FEATURES += lib/sqlite3/sqlite_stack.asm
MODULE_FEATURES += sqlite
MODULE_MK_sqlite = build/sqlite-proto.mk
endif
