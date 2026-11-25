# feature/kdebug owns this selection.
C_FEATURES += sys/kern/module/kdebug_call.c sys/kern/kdebug/kdebug_module.c
MODULE_FEATURES += kdebug
MODULE_MK_kdebug = build/kdebug.mk
