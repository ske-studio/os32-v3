# feature/fm owns this selection.
C_FEATURES += sys/dev/sound/fm/fm_module.c sys/hal/sound/snd_wake_tick.c
INCLUDE_FEATURES += sys/dev/sound/fm
MODULE_FEATURES += fm
MODULE_MK_fm = build/fm-module.mk
