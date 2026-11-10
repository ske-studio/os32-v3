# platform/pc98 owns this selection.
C_PLATFORM += platform/pc98/boot_irq.c init/boot_platform.c init/boot_display.c platform/pc98/emergency.c platform/pc98/boot.c init/boot_splash.c platform/pc98/splash.c platform/pc98/splash_image.c platform/pc98/clock.c platform/pc98/timer.c platform/pc98/lan_match.c platform/pc98/device_loop.c platform/pc98/device_ide.c platform/pc98/device_fs.c platform/pc98/device_pci.c platform/pc98/device_host.c platform/pc98/device_kcg.c platform/pc98/pci_resources.c platform/pc98/boot_disk.c platform/pc98/memory.c platform/pc98/d2_bind.c platform/pc98/boot_names.c platform/pc98/console.c platform/pc98/display.c platform/pc98/gfx_scroll.c platform/pc98/gfx_init.c platform/pc98/system.c platform/pc98/pic.c platform/pc98/irq_policy.c platform/pc98/irq_complete.c platform/pc98/boot_diag.c platform/pc98/console_attr.c
SUPPORTED_ARCH = x86
DEFAULT_FEATURE_VALUE = 1
C_PLATFORM += platform/pc98/pit.c platform/pc98/pit_math.c platform/pc98/pit_time.c platform/pc98/clock_source.c

C_PLATFORM += platform/pc98/bootinfo_capture.c platform/pc98/bootinfo_geom.c platform/pc98/bootinfo_report.c platform/pc98/bootinfo_check.c platform/pc98/bios_snapshot.c platform/pc98/boot_capture.c
