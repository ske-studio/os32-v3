# feature/kcg owns this selection.
C_FEATURES += drivers/video/kcg/kcg.c
INCLUDE_FEATURES += drivers/video/kcg
ifeq ($(PLATFORM),pc98)
C_FEATURES += drivers/video/kcg/kcg_module.c
MODULE_FEATURES += kcg
MODULE_MK_kcg = build/kcg-module.mk
else
C_FEATURES += kernel/dev/device_kcg.c
endif
