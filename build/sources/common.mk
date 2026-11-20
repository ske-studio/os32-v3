# 共通の場所の持ち物を像の機能ごとの組で宣言する (docs/design/build/select.md §3-2)。
# 核: 両方の arch の全部の像に入る。
C_COMMON_CORE = init/kernel_start.c kernel/boot_token.c mm/paging.c mm/physmem.c mm/pgalloc.c mm/memory_boot.c mm/shm.c mm/kmalloc.c kernel/sys.c kernel/idle_work.c kernel/io_work.c kernel/io_policy.c kernel/irq/tick_budget.c kernel/irq/tick.c kernel/time/tick.c fs/fd_redirect.c lib/kprintf.c lib/divdi3.c lib/kstring_c.c net/nic.c
C_COMMON_LINK = kernel/dev/device_host.c net/link.c net/link_notify.c net/host_mailbox.c net/nic_rx.c net/rx_work.c
C_COMMON_EXEC = kernel/ver_collect.c kernel/ver_format.c kernel/dev/device_fs.c kernel/time/ktime.c kernel/time/time_math.c init/bootinfo.c kernel/console/font_guard.c kernel/time/cpu_calibrate_math.c mm/paging_app.c input/hal/in2_dev.c input/hal/in2_state.c input/hal/mouse_ext.c input/hal/pointer_state.c input/hal/mouse_pointer.c input/hal/kbd_merge.c input/hal/legacy_keymap.c input/hal/keymap_registry.c input/hal/keymap_eval.c input/hal/keymap_state.c input/hal/in2_ring.c input/hal/in2_time.c kernel/input_legacy.c kernel/input_wait.c kernel/input_service.c kernel/time/clock_service.c kernel/console/console.c kernel/console/console_dual.c kernel/console/con_sink.c init/bootlog.c kernel/kbd_inject.c kernel/io_kapi.c kernel/console/font_source.c kernel/console/font_source_kcg.c lib/lz4.c kernel/console/unicode_file.c gfx/gfx_core.c gfx/gfx_dirty.c gfx/gfx_policy.c gfx/gfx_scroll.c gfx/palette.c fs/vfs.c fs/vfs_fd.c fs/pipe_buffer.c exec/exec.c exec/shell_service.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c exec/ring3_ls.c exec/redir_access.c exec/access_walk.c exec/surface_query.c exec/system_surface.c exec/lease.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c kapi/kapi_print.c kapi/kapi_strings.c kapi/kapi_sysinfo.c kapi/kapi_port.c kapi/kapi_host.c kapi/kapi_host_notice.c lib/path.c lib/utf8.c lib/kprintf_attr.c lib/os_time.c lib/kstring.c $(filter-out gfx/v2/a0_weak.c gfx/sync/a0_weak.c,$(wildcard gfx/v2/*.c gfx/sync/*.c)) drivers/base/dev.c kernel/dev/dev.c input/hal/legacy_ring.c input/hal/legacy_mode.c kernel/port_service.c kernel/serial_diag.c lib/crc32.c gfx/gfx_emergency.c
C_COMMON_USERSHELL = fs/serialfs/sfs_proto.c fs/serialfs/sfs_client.c fs/serialfs/serialfs.c fs/serialfs/serialfs_session.c fs/serialfs/serialfs_persistent.c
C_COMMON_USB = $(wildcard drivers/usb/usb_*.c drivers/usb/ohci_*.c drivers/usb/hid_*.c) drivers/usb/ehci_quiesce.c
C_COMMON_CLI = kernel/cli/resident_cli.c
# 共通の場所にあるが PowerPC の像にまだ入っていない物。include を直した順に本来の組へ移し、空になったら消す。
C_COMMON_PC98_PENDING = mm/dma_pool.c mm/dma_pool_math.c mm/shlib.c kernel/gui.c init/bootlog_save.c init/sysconfig.c kernel/kaudit.c init/kaudit_boot.c kernel/kselftest_report.c kernel/irq/irq.c kernel/irq/irq_math.c kernel/module/module_image.c kernel/module/module_runtime.c kernel/module/debug_probe.c lib/dma_contract.c gfx/gfx_hw_submit.c gfx/v2/a0_weak.c gfx/sync/a0_weak.c gfx/gfx_hw_mono_cpu.c gfx/gfx_osri.c gfx/gfx_osri_view.c fs/fat/fatfs/ff.c fs/fat/fatfs_vfs.c fs/ext2/ext2_super.c fs/ext2/ext2_inode.c fs/ext2/ext2_dir.c fs/ext2/ext2_file.c fs/ext2/ext2_vfs.c fs/serialfs/sfs_session_fg.c kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_osri.c lib/kutf16.c lib/kmath.c
C_COMMON_EXEC += drivers/pci/pci.c drivers/pci/pci_barsize.c drivers/pci/pci_decode.c drivers/pci/pci_bind.c drivers/pci/pci_bind_match.c kernel/dev/device_pci.c
C_EXT2_FORMATTER := fs/ext2/ext2_fmt.c fs/ext2/ext2_layout.c fs/ext2/ext2_time.c
ifeq ($(PLATFORM),pc98)
C_COMMON_PC98_PENDING += $(C_EXT2_FORMATTER)
endif
C_COMMON_COMPILE_ONLY_PPC := kernel/sqlite_engine.c $(C_EXT2_FORMATTER)
C_COMMON_LOOP = kernel/dev/device_loop.c drivers/block/loop_dev.c
ifeq ($(FEATURE_PC98_PENDING)$(FEATURE_LOOP),11)
C_COMMON_LOOP += fs/ext2/ext2_medium_loop.c
C_ORDER_TAIL += fs/ext2/ext2_medium_loop.c
endif
C_COMMON = $(C_COMMON_CORE) $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(C_COMMON_$(f))))
INCLUDE_COMMON += kernel include init kernel/console kernel/irq kernel/module kernel/time mm kernel/dev fs/ext2 fs/fat fs/isofs fs/serialfs
