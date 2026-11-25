# feature/pc98-devices owns this selection.
# 装置の側の接着: PC-98 の planar/PEGC の VRAM への提示、FatFs の装置層 (FDC・dev)。
C_FEATURES += sys/platform/pc98/video/gfx_vram.c sys/dev/block/fat_diskio.c
# NP21/W の HostDrv (エミュレータの I/O ポートの口) の FS。
C_FEATURES += sys/dev/hostdrv/hostdrvfs.c
C_FEATURES += sys/platform/pc98/input/kbd.c sys/platform/pc98/input/kbd_hal.c sys/dev/input/kbd_status.c sys/dev/input/kbd_dlog.c sys/dev/char/serial.c sys/dev/char/serial_plan.c sys/dev/block/fdc.c sys/dev/block/fdc_decide.c sys/dev/block/fdc_track.c sys/dev/dma/dma8237.c sys/dev/dma/dma8237_math.c sys/dev/pci/pci_hwb.c sys/fs/pc98pt.c sys/dev/char/rtc.c sys/dev/char/np2sysp.c sys/platform/pc98/input/mouse.c sys/platform/pc98/input/mouse_bus.c sys/platform/pc98/input/mouse_hal.c sys/dev/input/mouse_seamless.c sys/dev/net/ne2000/lgy98.c sys/dev/net/ne2000/ne2000.c sys/dev/net/ne2000/ne2000_ring.c sys/platform/pc98/video/pc98_route.c sys/dev/net/ne2000/nic_lgy98.c sys/platform/pc98/video/backend_pc98.c sys/platform/pc98/video/backend_pc98_osri.c sys/platform/pc98/video/backend_pegc.c sys/platform/pc98/video/pegc_v2.c sys/platform/pc98/video/splash_raster.c sys/platform/pc98/video/splash_raster_image.c sys/platform/pc98/video/splash_raster_plan.c sys/platform/pc98/video/splash_raster_play.c sys/platform/pc98/video/splash_raster_run.c
ASM_FEATURES += sys/dev/net/ne2000/ne2000_io.asm
INCLUDE_FEATURES += sys/dev/hostdrv sys/dev/ata sys/dev/block sys/platform/pc98 sys/dev/char sys/dev/dma sys/dev/input sys/platform/pc98/input sys/dev/net/ne2000 sys/dev/pci sys/platform/pc98/video
ifeq ($(SPLASH_DIAG_BUILD),1)
DIAGNOSTIC_SPLASH_DIAG_BUILD = sys/platform/pc98/splash_probe.c
DIAGNOSTIC_FLAG_SPLASH_DIAG_BUILD = -DSPLASH_RASTER_DIAG
endif
ifeq ($(SPLASH_OBS_BUILD),1)
DIAGNOSTIC_SPLASH_OBS_BUILD = sys/platform/pc98/splash_obs.c
DIAGNOSTIC_FLAG_SPLASH_OBS_BUILD = -DSPLASH_RASTER_OBS
endif
