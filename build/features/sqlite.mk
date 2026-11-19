# feature/sqlite owns this selection.
C_FEATURES += kernel/sqlite_engine.c kernel/sqlite_outer.c lib/sqlite3/os32_sqlite_test.c
ifeq ($(ARCH),ppc)
GAS_ARCH_EXEC += arch/ppc/sqlite_stack_ppc.S
else
ASM_FEATURES += lib/sqlite3/sqlite_stack.asm
endif
MODULE_FEATURES += sqlite
MODULE_MK_sqlite = build/sqlite-proto.mk
