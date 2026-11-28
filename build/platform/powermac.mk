SUPPORTED_ARCH = ppc
DEFAULT_FEATURE_VALUE = 0
FEATURE_KDEBUG ?= $(FEATURE_EXEC)
FEATURE_SQLITE ?= $(FEATURE_EXEC)
FEATURE_FEP ?= $(FEATURE_SQLITE)
FEATURE_KCG ?= $(FEATURE_EXEC)
# kboot の像の持ち物 (像の機能ごと)。共通の場所の物のうち、今は PowerPC の像だけが組む物も持つ。
C_PLATFORM_CORE = sys/platform/powermac/rh_observe.c sys/platform/powermac/ofw.c sys/platform/powermac/escc.c sys/platform/powermac/escc_tree.c sys/platform/powermac/kboot_capture.c sys/platform/powermac/via_tree.c sys/platform/powermac/system.c sys/dev/macintosh/via.c sys/platform/powermac/boot_token_powermac.c sys/platform/powermac/gmac_tree.c sys/platform/powermac/gmac_clock.c sys/platform/powermac/kboot_reserve.c sys/platform/powermac/memory.c sys/platform/powermac/mpic_tree.c sys/platform/powermac/mpic.c sys/platform/powermac/clock.c sys/platform/powermac/tick_budget.c sys/platform/powermac/log.c sys/platform/powermac/pci_config.c sys/platform/powermac/gmac_dma.c sys/platform/powermac/lan_gem_io.c sys/platform/powermac/lan_log.c sys/platform/powermac/lan_monitor.c sys/platform/powermac/lan_diag.c sys/platform/powermac/lan_service.c sys/platform/powermac/ppc_kernel.c sys/platform/powermac/hwb_window.c $(if $(filter 1,$(FEATURE_KDEBUG)),sys/platform/powermac/hwb_resource.c) sys/dev/net/gem/gem_reset.c sys/dev/net/gem/gem_phy.c sys/dev/net/gem/gem_ring.c sys/dev/net/gem/gem_mac.c sys/dev/net/gem/gem_tx.c sys/dev/net/gem/gem_rx.c
C_PLATFORM_CORE += sys/platform/powermac/machcfg.c
C_PLATFORM_CORE += sys/platform/powermac/log_printf.c
ifeq ($(filter powermac kboot-link-sh,$(IMAGE)):$(filter 1,$(KBOOT_SHELL_LAYOUT_DIAG)),$(IMAGE):)
C_PLATFORM_CORE += sys/platform/powermac/report_product.c
else
C_PLATFORM_CORE += sys/platform/powermac/report_diag.c
endif
C_PLATFORM_LINK = sys/platform/powermac/link_platform.c sys/platform/powermac/nic_gem.c sys/platform/powermac/link_log.c sys/platform/powermac/link_diag.c
C_PLATFORM_NOLINK = sys/platform/powermac/lan_wire.c sys/platform/powermac/lan_terminal.c sys/platform/powermac/terminal_input.c sys/platform/powermac/host_unavailable.c
C_PLATFORM_EXEC = sys/kern/module/kdebug_call.c sys/platform/powermac/ver_backend.c sys/platform/powermac/pci_backend.c sys/platform/powermac/root_boot.c sys/platform/powermac/root_image.c sys/platform/powermac/module_pair.c sys/platform/powermac/console_stream.c sys/platform/powermac/exec_boot.c sys/platform/powermac/console.c sys/platform/powermac/console_text.c sys/platform/powermac/display.c sys/platform/powermac/display_provider.c sys/platform/powermac/display_bundle.c sys/platform/powermac/display_dual.c sys/fs/isofs/iso9660.c sys/fs/isofs/iso9660_media.c sys/kern/dev/ram_ro.c sys/kern/init/boot_root.c sys/fs/ramfs/ramfs.c sys/kern/console/fbcon.c sys/platform/powermac/kcg.c sys/dev/video/of_fb/backend_of_fb.c sys/dev/video/of_fb/of_fb_v2.c sys/kern/console/gfx_console.c
C_PLATFORM_EXEC += sys/kern/gfx/osri/gfx_osri.c sys/platform/powermac/osri_view.c sys/kapi/kapi_osri.c
C_PLATFORM_NOEXEC = sys/platform/powermac/pci_unavailable.c sys/platform/powermac/kboot_console.c
C_PLATFORM_USERSHELL = sys/platform/powermac/shell_loop_ops.c sys/platform/powermac/input_boot.c sys/platform/powermac/hostfs_boot.c sys/input/keymaps/hid_jp.c $(if $(filter 1,$(FEATURE_LINK)),sys/platform/powermac/hostfs_link_io.c sys/platform/powermac/hostfs_link_peer.c,sys/platform/powermac/hostfs_io.c sys/platform/powermac/hostfs_peer.c)
C_PLATFORM_USB = sys/platform/powermac/usb_key_report.c sys/platform/powermac/input_usb.c sys/platform/powermac/input_keymap_backend.c sys/platform/powermac/usb_tree.c sys/platform/powermac/usb_platform.c sys/platform/powermac/usb_boot.c
C_PLATFORM_LOOP = sys/platform/powermac/loop_boot.c
C_PLATFORM_IDE = sys/platform/powermac/ide_tree.c sys/platform/powermac/ide_boot.c
C_PLATFORM_IDE += sys/dev/ata/atapi.c sys/dev/ata/cd_body.c
C_PLATFORM_IDE += sys/platform/powermac/apm.c sys/platform/powermac/partition_view.c sys/platform/powermac/install_backend.c
C_PLATFORM_IDE += sys/fs/ext2/ext2_super.c sys/fs/ext2/ext2_inode.c sys/fs/ext2/ext2_dir.c sys/fs/ext2/ext2_file.c sys/fs/ext2/ext2_vfs.c $(C_EXT2_FORMATTER)
