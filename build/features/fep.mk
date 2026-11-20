# feature/fep owns this selection.
C_FEATURES += kernel/ime/fep_module.c kernel/ime/fep_suspend_owner.c
INCLUDE_FEATURES += kernel/ime
# The packed module uses an i386 linker script and the PC-98 display backend.
ifeq ($(ARCH),x86)
C_FEATURES += kernel/ime/fep_static.c
MODULE_FEATURES += fep
MODULE_MK_fep = build/fep-module.mk
endif
