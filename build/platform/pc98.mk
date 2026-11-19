# platform/pc98 owns this selection.
C_PLATFORM += platform/pc98/kernel.c platform/pc98/boot_token_pc98.c input/keymaps/pc98_jp.c input/keymaps/hid_jp.c platform/pc98/input_keymap.c platform/pc98/boot_irq.c platform/pc98/boot_platform.c platform/pc98/boot_display.c platform/pc98/emergency.c platform/pc98/boot.c platform/pc98/boot_splash.c platform/pc98/splash.c platform/pc98/splash_image.c platform/pc98/clock.c platform/pc98/timer.c platform/pc98/lan_match.c platform/pc98/device_ide.c platform/pc98/device_host.c kernel/dev/device_kcg.c platform/pc98/pci_config.c platform/pc98/pci_resources.c platform/pc98/boot_disk.c platform/pc98/memory.c platform/pc98/d2_bind.c platform/pc98/boot_names.c platform/pc98/console.c platform/pc98/display.c platform/pc98/display_dual.c platform/pc98/gfx_scroll.c platform/pc98/gfx_init.c platform/pc98/gfx_selector.c platform/pc98/gfx_surfaces.c platform/pc98/display_bundle.c platform/pc98/link_platform.c platform/pc98/system.c platform/pc98/pic.c platform/pc98/irq_policy.c platform/pc98/irq_complete.c platform/pc98/boot_diag.c platform/pc98/ver_backend.c platform/pc98/console_attr.c
SUPPORTED_ARCH = x86
C_PLATFORM += platform/pc98/tsc_math.c platform/pc98/tsc_clock.c
DEFAULT_FEATURE_VALUE = 1
C_PLATFORM += platform/pc98/pit.c platform/pc98/tick_budget.c platform/pc98/pit_math.c platform/pc98/pit_time.c platform/pc98/clock_source.c

C_PLATFORM += platform/pc98/bootinfo_capture.c platform/pc98/bootinfo_geom.c platform/pc98/bootinfo_report.c platform/pc98/bootinfo_check.c platform/pc98/bios_snapshot.c platform/pc98/boot_capture.c
