# feature/kcg owns this selection.
C_FEATURES += sys/dev/video/kcg/kcg.c
INCLUDE_FEATURES += sys/dev/video/kcg
ifeq ($(PLATFORM),pc98)
C_FEATURES += sys/dev/video/kcg/kcg_module.c
MODULE_FEATURES += kcg
MODULE_MK_kcg = build/kcg-module.mk
else
C_FEATURES += sys/dev/kcg/device_kcg.c
endif
