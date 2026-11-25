# feature/sqlite owns this selection.
C_FEATURES += sys/kern/sqlite_engine.c sys/kern/sqlite_outer.c
ifeq ($(ARCH),ppc)
GAS_ARCH_EXEC += sys/arch/ppc/sqlite_stack_ppc.S
include sys/conf/features/sqlite-static.mk
else
C_FEATURES += lib/sqlite3/os32_sqlite_test.c
ASM_FEATURES += lib/sqlite3/sqlite_stack.asm
MODULE_FEATURES += sqlite
MODULE_MK_sqlite = build/sqlite-proto.mk
endif
