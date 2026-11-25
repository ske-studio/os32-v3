# feature/trident owns this selection.
C_FEATURES += sys/dev/video/trident/trident_module.c sys/dev/video/trident/resources.c
MODULE_FEATURES += trident
MODULE_MK_trident = build/trident-module.mk
