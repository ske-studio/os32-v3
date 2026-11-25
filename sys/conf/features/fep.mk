# feature/fep owns this selection.
C_FEATURES += sys/kern/fep_boot.c sys/kern/ime/fep_module.c
INCLUDE_FEATURES += sys/kern/ime
# The packed module uses an i386 linker script and the PC-98 display backend.
ifeq ($(ARCH),x86)
MODULE_FEATURES += fep
MODULE_MK_fep = build/fep-module.mk
endif
ifeq ($(ARCH),ppc)
C_FEATURES += sys/platform/powermac/fep_imports.c
endif
