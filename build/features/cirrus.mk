# feature/cirrus owns this selection.
C_FEATURES += drivers/video/cirrus/cirrus_module.c drivers/video/cirrus/cirrus_identify.c
INCLUDE_FEATURES += drivers/video/cirrus
MODULE_FEATURES += cirrus
MODULE_MK_cirrus = build/cirrus-module.mk
