# feature/fm owns this selection.
C_FEATURES += drivers/sound/fm/fm_module.c drivers/sound/snd_wake_tick.c
INCLUDE_FEATURES += drivers/sound/fm
MODULE_FEATURES += fm
MODULE_MK_fm = build/fm-module.mk
