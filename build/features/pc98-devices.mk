# feature/pc98-devices owns this selection.
# 装置の側の接着: PC-98 の planar/PEGC の VRAM への提示、FatFs の装置層 (FDC・dev)。
C_FEATURES += drivers/video/pc98/gfx_vram.c drivers/block/fat_diskio.c
# NP21/W の HostDrv (エミュレータの I/O ポートの口) の FS。
C_FEATURES += drivers/hostdrv/hostdrvfs.c
C_FEATURES += drivers/input/kbd.c drivers/input/kbd_hal.c drivers/input/kbd_status.c drivers/input/kbd_dlog.c drivers/char/serial.c drivers/char/serial_plan.c drivers/block/fdc.c drivers/block/fdc_decide.c drivers/block/fdc_track.c drivers/dma/dma8237.c drivers/dma/dma8237_math.c drivers/pci/pci_hwb.c drivers/ata/ide.c drivers/ata/ide_addr.c fs/pc98pt.c drivers/char/rtc.c drivers/char/np2sysp.c drivers/input/mouse.c drivers/input/mouse_bus.c drivers/input/mouse_hal.c drivers/input/mouse_seamless.c drivers/net/ne2000/lgy98.c drivers/net/ne2000/ne2000.c drivers/net/ne2000/ne2000_ring.c drivers/video/pc98/pc98_route.c drivers/net/ne2000/nic_lgy98.c drivers/video/pc98/backend_pc98.c drivers/video/pc98/backend_pc98_osri.c drivers/video/pc98/backend_pegc.c drivers/video/pc98/pegc_v2.c drivers/video/pc98/splash_raster.c drivers/video/pc98/splash_raster_image.c drivers/video/pc98/splash_raster_plan.c drivers/video/pc98/splash_raster_play.c drivers/video/pc98/splash_raster_run.c
ASM_FEATURES += drivers/net/ne2000/ne2000_io.asm
INCLUDE_FEATURES += drivers/hostdrv drivers/ata drivers/block drivers/char drivers/dma drivers/input drivers/net/ne2000 drivers/pci drivers/video/pc98
ifeq ($(SPLASH_DIAG_BUILD),1)
DIAGNOSTIC_SPLASH_DIAG_BUILD = platform/pc98/splash_probe.c
DIAGNOSTIC_FLAG_SPLASH_DIAG_BUILD = -DSPLASH_RASTER_DIAG
endif
ifeq ($(SPLASH_OBS_BUILD),1)
DIAGNOSTIC_SPLASH_OBS_BUILD = platform/pc98/splash_obs.c
DIAGNOSTIC_FLAG_SPLASH_OBS_BUILD = -DSPLASH_RASTER_OBS
endif
