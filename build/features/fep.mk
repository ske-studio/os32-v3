# feature/fep owns this selection.
C_FEATURES += kernel/fep_boot.c kernel/ime/fep_module.c
INCLUDE_FEATURES += kernel/ime
# The packed module uses an i386 linker script and the PC-98 display backend.
ifeq ($(ARCH),x86)
MODULE_FEATURES += fep
MODULE_MK_fep = build/fep-module.mk
endif
ifeq ($(ARCH),ppc)
C_FEATURES += platform/powermac/fep_imports.c
endif
