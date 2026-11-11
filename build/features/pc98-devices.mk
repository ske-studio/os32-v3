# feature/pc98-devices owns this selection.
C_FEATURES += kernel/port_service.c kernel/serial_diag.c drivers/input/kbd.c drivers/input/kbd_status.c drivers/input/kbd_dlog.c drivers/char/serial.c drivers/char/serial_plan.c drivers/block/fdc.c drivers/block/fdc_decide.c drivers/block/fdc_track.c drivers/dma/dma8237.c drivers/dma/dma8237_math.c drivers/pci/pci.c drivers/pci/pci_barsize.c drivers/pci/pci_hwb.c drivers/pci/pci_decode.c drivers/pci/pci_bind.c drivers/pci/pci_bind_match.c drivers/ata/ide.c drivers/ata/ide_addr.c drivers/block/pc98pt.c drivers/char/rtc.c drivers/base/dev.c drivers/char/np2sysp.c drivers/block/loop_dev.c drivers/input/mouse.c drivers/input/mouse_bus.c drivers/input/mouse_seamless.c drivers/net/ne2000/lgy98.c drivers/net/ne2000/ne2000.c drivers/net/ne2000/ne2000_ring.c drivers/video/pc98/pc98_route.c net/link.c net/link_notify.c net/host_mailbox.c net/nic.c net/nic_rx.c net/nic_lgy98.c drivers/video/pc98/backend_pc98.c drivers/video/pc98/backend_pc98_osri.c drivers/video/pc98/backend_pegc.c drivers/video/pc98/pegc_v2.c drivers/video/pc98/splash_raster.c drivers/video/pc98/splash_raster_image.c drivers/video/pc98/splash_raster_plan.c drivers/video/pc98/splash_raster_play.c drivers/video/pc98/splash_raster_run.c
ASM_FEATURES += drivers/net/ne2000/ne2000_io.asm
INCLUDE_FEATURES += drivers/ata drivers/block drivers/char drivers/dma drivers/input drivers/net/ne2000 drivers/pci drivers/video/pc98
ifeq ($(SPLASH_DIAG_BUILD),1)
DIAGNOSTIC_SPLASH_DIAG_BUILD = platform/pc98/splash_probe.c
DIAGNOSTIC_FLAG_SPLASH_DIAG_BUILD = -DSPLASH_RASTER_DIAG
endif
ifeq ($(SPLASH_OBS_BUILD),1)
DIAGNOSTIC_SPLASH_OBS_BUILD = platform/pc98/splash_obs.c
DIAGNOSTIC_FLAG_SPLASH_OBS_BUILD = -DSPLASH_RASTER_OBS
endif
