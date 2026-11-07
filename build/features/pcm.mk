# feature/pcm owns this selection.
C_FEATURES += drivers/sound/cs4231/pcm_module.c
INCLUDE_FEATURES += drivers/sound/cs4231
MODULE_FEATURES += pcm
MODULE_MK_pcm = build/pcm-module.mk
ifeq ($(PCM_E4_DIAG_BUILD),1)
DIAGNOSTIC_PCM_E4_DIAG_BUILD = drivers/sound/cs4231/pcm_e4_diag.c drivers/sound/cs4231/pcm_e4_guest.c
DIAGNOSTIC_FLAG_PCM_E4_DIAG_BUILD = -DPCM_E4_DIAG
endif
