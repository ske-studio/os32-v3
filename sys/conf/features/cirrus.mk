# feature/cirrus owns this selection.
C_FEATURES += sys/dev/video/cirrus/cirrus_module.c sys/platform/pc98/video/cirrus/cirrus_identify.c
INCLUDE_FEATURES += sys/dev/video/cirrus sys/platform/pc98/video/cirrus
MODULE_FEATURES += cirrus
MODULE_MK_cirrus = build/cirrus-module.mk
