# feature/geforce owns this selection.
C_FEATURES += kdebug/gf5_test.c kdebug/gf_display_test.c drivers/video/geforce/edid.c drivers/video/geforce/tv_data.c drivers/video/geforce/gf_display_data.c drivers/video/geforce/geforce_module.c drivers/video/geforce/geforce_platform.c drivers/video/geforce/gf_wc.c
INCLUDE_FEATURES += drivers/video/geforce
MODULE_FEATURES += geforce
MODULE_MK_geforce = build/geforce-module.mk
