# feature/geforce owns this selection.
C_FEATURES += sys/dev/video/geforce/diag/gf5_test.c sys/dev/video/geforce/diag/gf_display_test.c sys/dev/video/geforce/edid.c sys/dev/video/geforce/tv_data.c sys/dev/video/geforce/gf_display_data.c sys/dev/video/geforce/geforce_module.c sys/dev/video/geforce/geforce_platform.c sys/dev/video/geforce/gf_wc.c
INCLUDE_FEATURES += sys/dev/video/geforce sys/platform/pc98/video/geforce
MODULE_FEATURES += geforce
MODULE_MK_geforce = build/geforce-module.mk
