# The module and kernel share the actual machine/ABI/optimization flags.
ifeq ($(origin KERNEL_CFLAGS),undefined)
include build/config.mk
endif
KDEBUG_OUT ?= $(BUILD_OUT)/kdebug
KDEBUG_STAGE := S6
KDEBUG_GEN := sys/platform/pc98/kdebug/generated
KDEBUG_STUB := sys/arch/x86/kdebug/generated/kdebug_import_stubs.S
MODULE_SCHEMA := sys/kern/module/module_schema.h
$(MODULE_SCHEMA): tools/module_schema.py tools/gen_module_schema.py
	python3 tools/gen_module_schema.py

KDEBUG_OBJS := $(KDEBUG_OUT)/v86_gcap.o $(KDEBUG_OUT)/v86_gcap_math.o $(KDEBUG_OUT)/nic_trace.o $(if $(filter 1,$(GFX_MEASURE_DIAG_BUILD)),$(KDEBUG_OUT)/gfx_transfer_bench.o $(KDEBUG_OUT)/gfx_snapshot.o) $(KDEBUG_OUT)/kselftest.o $(KDEBUG_OUT)/selftest_kbd_inject.o $(KDEBUG_OUT)/selftest_lease.o $(KDEBUG_OUT)/selftest_exec.o $(KDEBUG_OUT)/crt_diag.o $(KDEBUG_OUT)/kdebug_main.o \
               $(KDEBUG_OUT)/probe.o $(KDEBUG_OUT)/probe_index.o $(KDEBUG_OUT)/hwb_access.o $(KDEBUG_OUT)/kpeek.o $(KDEBUG_OUT)/kdebug_window.o $(KDEBUG_OUT)/recover.o $(KDEBUG_OUT)/recover_profile.o $(KDEBUG_OUT)/recover_virtual.o \
               $(KDEBUG_OUT)/trident_command_test.o $(KDEBUG_OUT)/kdebug_import_slots.o $(KDEBUG_OUT)/kdebug_import_stubs.o
KDEBUG_OBJS += $(KDEBUG_OUT)/recover_gf_locate.o $(KDEBUG_OUT)/pci_locate.o
$(KDEBUG_OUT)/pci_locate.o: sys/dev/pci/pci_locate.c build/config.mk build/kdebug.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@
KDEBUG_KEXPORT_OBJ := $(KDEBUG_OUT)/kdebug_kexports.o
ifeq ($(TICK_PROBE_BUILD),1)
KDEBUG_OBJS += $(KDEBUG_OUT)/tick_probe.o
endif
C_KERNEL_OBJ += $(KDEBUG_KEXPORT_OBJ)
# kernel.elf's prerequisites were expanded before this include.
$(BUILD_OUT)/kernel.elf: $(KDEBUG_KEXPORT_OBJ)

# Generate all contracts from the same .def and source declarations.
KDEBUG_DEF_SOURCES := $(shell python3 -c 'import json; print(" ".join(sorted({e["source"] for e in json.load(open("sys/kern/module/kdebug_imports.def"))["imports"] if "source" in e})))')
# Keep generated source inputs in the tree, like the KAPI contract: read-only
# static checks (including CI without a cross compiler) need these under -n.
KDEBUG_GENERATED := $(addprefix $(KDEBUG_GEN)/,kdebug_imports.h kdebug_imports.json \
    kdebug_kexports.c kdebug_import_slots.c kdebug_import_macros.h) $(KDEBUG_STUB)
$(KDEBUG_GENERATED) &: sys/kern/module/kdebug_imports.def tools/gen_kdebug_imports.py tools/mkmod.py sdk/target_profile.py tools/module_schema.py build/kdebug.mk $(MODULE_SCHEMA) $(KDEBUG_DEF_SOURCES)
	python3 tools/gen_kdebug_imports.py --stage $(KDEBUG_STAGE) --out-dir $(KDEBUG_GEN) --stub $(KDEBUG_STUB)

$(KERNEL_OBJ_PREFIX)sys/kern/kdebug/kdebug_module.o: sys/kern/kdebug/kdebug_module.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_imports.h $(MODULE_SCHEMA)
	@mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(KDEBUG_GEN) -c sys/kern/kdebug/kdebug_module.c -o $@

$(KDEBUG_OUT)/recover_profile.o: KERNEL_CFLAGS += -DRECOVER_MODULE

$(KDEBUG_OUT)/%.o: sys/platform/pc98/kdebug/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: sys/dev/video/geforce/diag/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: sys/kern/kdebug/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: sys/arch/x86/kdebug/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: sys/dev/char/kdebug/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: sys/dev/video/trident/diag/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/kdebug_kexports.o $(KDEBUG_OUT)/kdebug_import_slots.o: $(KDEBUG_OUT)/%.o: $(KDEBUG_GEN)/%.c $(KDEBUG_GEN)/kdebug_imports.h build/config.mk build/kdebug.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(KDEBUG_GEN) -c $< -o $@

$(KDEBUG_OUT)/kdebug_import_stubs.o: $(KDEBUG_STUB) build/config.mk build/kdebug.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@

$(KDEBUG_OUT)/kdebug.elf: $(KDEBUG_OBJS) build/module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -Map=$(KDEBUG_OUT)/kdebug.map -T build/module.ld $(KDEBUG_OBJS) $(KDEBUG_OUT)/hwdev_core.o -o $@

# Values come from their authoritative definitions, including before S1b's bump.
KDEBUG_KAPI_GEN = $(shell python3 -c 'import json; print(json.load(open("sdk/kapi.json"))["version"])')
KDEBUG_LAYOUT_GEN = $(shell sed -n 's/^\#define OS32_MEMORY_LAYOUT_GENERATION \([0-9]*\).*/\1/p' sdk/include/os32/os32_generations.h)

define pack_kdebug
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --elf $(KDEBUG_OUT)/kdebug.elf --metadata $(KDEBUG_GEN)/kdebug_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef

$(BUILD_OUT)/kdebug.bin: $(KDEBUG_OUT)/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json sdk/include/os32/os32_generations.h build/kdebug.mk
	$(call pack_kdebug,$(BUILD_OUT)/vmkernel.lz4)

$(R1_OUT)/kdebug.bin: $(KDEBUG_OUT)/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json sdk/include/os32/os32_generations.h build/kdebug.mk
	$(call pack_kdebug,$(R1_OUT)/vmkernel.lz4)

$(R1_OBJ)/sys/kern/kdebug/kdebug_module.o: sys/kern/kdebug/kdebug_module.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_imports.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) -DOS32_R1_FIXTURE $(INC_KERNEL) -I$(KDEBUG_GEN) -c sys/kern/kdebug/kdebug_module.c -o $@
$(R1_OUT)/kernel.elf: $(KDEBUG_KEXPORT_OBJ)
R1_OBJECTS += $(KDEBUG_KEXPORT_OBJ)

all: $(BUILD_OUT)/kdebug.bin
$(R1_OBJ)/sys/kern/kdebug/kdebug_module.o \
    $(R1_OBJ)/sys/kern/module/module_image.o $(R1_OBJ)/sys/kern/module/module_runtime.o \
    $(KERNEL_OBJ_PREFIX)sys/kern/kdebug/kdebug_module.o $(KERNEL_OBJ_PREFIX)sys/kern/module/module_image.o \
    $(KERNEL_OBJ_PREFIX)sys/kern/module/module_runtime.o: $(MODULE_SCHEMA)
-include $(KDEBUG_OBJS:.o=.d) $(KDEBUG_KEXPORT_OBJ:.o=.d)

# One shared contract, compiled with the kernel machine/ABI flags.
HWDEV_GEN := sys/dev/hwdev/generated
HWDEV_HEADERS := sys/include/font_fault_contract.h sys/include/kdebug_resident_contract.h sys/platform/pc98/platform_gfx_init.h sys/include/recover_window_contract.h sys/include/kdebug_window_contract.h sys/dev/video/geforce/accel.h sys/dev/video/geforce/accel_service.h sys/dev/video/geforce/command.h sys/dev/video/geforce/feature5.h sys/dev/video/geforce/foundation.h sys/dev/video/geforce/gf1_test.h sys/dev/video/geforce/gf2_test.h sys/dev/video/geforce/internal.h sys/dev/video/geforce/production.h sys/dev/video/geforce/quiesce_rows.h sys/dev/video/geforce/scripts.h sys/dev/video/geforce/stop_state.h sys/dev/video/geforce/vram_layout.h sys/dev/video/geforce/vram_view.h sys/dev/video/geforce/wait_diag.h sys/dev/video/geforce/display/access.h sys/dev/video/geforce/display/api.h sys/dev/video/geforce/display/calc.h sys/dev/video/geforce/display/data.h sys/dev/video/geforce/display/hw_state.h sys/dev/video/geforce/display/probes.h sys/dev/video/geforce/display/stage.h sys/dev/video/geforce/display/state.h sys/dev/video/geforce/display/timeout.h $(wildcard sys/dev/video/geforce/op/op/*.h) sys/include/abi.h sys/include/app_ids.h sys/include/appmem_types.h sys/include/atapi_contract.h sys/include/av_contract.h sys/include/boot_cpu.h sys/include/boot_diag.h sys/include/boot_root.h sys/include/boot_token.h sys/include/bootinfo.h sys/include/bootlog.h sys/include/build_id.h sys/include/callconv.h sys/hal/time/clock_service.h sys/hal/time/clock_source.h sys/include/con_sink.h sys/include/config.h sys/include/console.h sys/include/console_attr.h sys/include/console_output.h sys/include/copy_units.h sys/include/cpu.h sys/include/debug_probe.h sys/include/debug_probe_snapshot.h sys/include/diag_stack.h sys/include/display_provider.h sys/include/dma_contract.h sys/include/endian_dma.h sys/include/endian_le.h sys/include/exec_arch.h sys/include/exec_selftest.h sys/include/exec_wait_contract.h sys/include/ext2_medium_contract.h sys/include/fault_fixture.h sys/include/fdc_contract.h sys/include/fm_contract.h sys/include/geforce_contract.h sys/include/gf_production.h sys/include/gfx_format.h sys/include/gfx_hal.h sys/hal/gfx/gfx_hal2.h sys/include/gfx_mode.h sys/hal/gfx/gfx_present_contract.h sys/include/gfx_sync.h sys/include/ide_bus.h sys/include/ide_contract.h sys/include/ime_boot.h sys/include/ime_control.h sys/hal/input/input_service.h sys/include/install_access.h sys/include/install_management.h sys/include/install_medium.h sys/include/io.h sys/include/irq_line.h sys/include/kapi_call.h sys/include/kapi_call_generated.h sys/include/kapi_print.h sys/include/kapi_strings.h sys/include/kbd.h sys/include/kbd_contract.h sys/include/kbd_inject.h sys/include/kcg_contract.h sys/include/kernel_mount_children.h sys/include/kernel_tick.h sys/include/kprintf.h sys/include/kselftest_probe.h sys/include/ksetjmp.h sys/include/lan_contract.h sys/include/launch.h sys/include/legacy_guest.h sys/include/loop_contract.h sys/include/machcfg_contract.h sys/include/memmap.h sys/hal/input/mouse_pointer_contract.h sys/include/net_eth.h sys/include/os_time.h sys/platform/pc98/pc98.h sys/include/pci_bind_contract.h sys/include/pci_config_contract.h sys/include/pci_contract.h sys/include/pci_dev_contract.h sys/include/pci_hwb_contract.h sys/include/pcm_contract.h sys/platform/pc98/pegc.h sys/include/platform_boot.h sys/include/platform_device.h sys/include/platform_display.h sys/include/platform_emergency.h sys/include/platform_host.h sys/include/platform_install_boot.h sys/include/platform_irq.h sys/include/platform_memory.h sys/include/platform_module_pair.h sys/include/platform_splash.h sys/include/platform_sys.h sys/include/platform_timer.h sys/hal/input/pointer_service.h sys/include/port_service.h sys/include/redir_access.h sys/include/remote_control.h sys/include/serial_contract.h sys/include/shell_loop.h sys/hal/sound/snd_wake_contract.h sys/include/snd_writer_probe.h sys/include/sys.h sys/include/syscall_code.h sys/include/text_screen.h sys/include/time_units.h sys/include/type_contract.h sys/include/types.h sys/include/usb_platform.h sys/include/user_read_contract.h sys/arch/x86/v86_call.h sys/include/v86_contract.h sys/include/ver_backend_contract.h sys/include/video_service.h sys/include/video_wc.h sys/include/vk32.h sys/platform/pc98/wab_xe10.h sys/kern/fep_boot.h sys/kern/gui.h sys/kern/idle_work.h sys/kern/io_clock.h sys/kern/io_kapi.h sys/kern/io_policy.h sys/kern/io_work.h sys/kern/kselftest.h sys/kern/kstack_hw.h sys/kern/r1_fixture.h sys/kern/sqlite_engine.h sys/kern/sqlite_engine_body.h sys/kern/ver_info.h sys/kern/console/console_backend.h sys/kern/console/console_dual.h sys/kern/console/console_stream.h sys/kern/console/display_restore.h sys/kern/console/fbcon.h sys/kern/console/font_guard.h sys/kern/console/font_source.h sys/kern/console/font_source_kcg.h sys/kern/console/unicode_file.h sys/kern/ime/fep_module.h sys/kern/ime/ime.h sys/kern/ime/ime_display_private.h sys/kern/ime/ime_render.h sys/kern/ime/ime_store.h sys/kern/irq/irq.h sys/kern/irq/irq_math.h sys/kern/irq/tick.h sys/kern/irq/tick_budget.h sys/kern/module/module.h sys/kern/module/module_image.h sys/kern/module/module_runtime.h sys/kern/module/module_schema.h sys/hal/time/cpu_calibrate.h sys/kern/time/cpu_calibrate_math.h sys/kern/time/ktime.h sys/hal/time/tick_clock.h sys/kern/time/tick_probe.h sys/kern/time/tick_probe_ids.h sys/kern/time/time_math.h sys/kern/v86/v86.h sys/kern/v86/v86_gcap.h sys/kern/v86/v86_gcap_math.h sys/kern/v86/v86_mem.h sys/kern/v86/v86_module.h sys/platform/pc98/legacy/v86.h sys/platform/pc98/legacy/v86_bios.h sys/platform/pc98/legacy/v86_gcap.h sys/platform/pc98/legacy/v86_gcap_math.h sys/platform/pc98/legacy/v86_io.h sys/platform/pc98/legacy/v86_kbd.h sys/platform/pc98/legacy/v86_mem.h sys/platform/pc98/legacy/v86_module.h sys/platform/pc98/legacy/v86_pic.h sys/kern/init/boot_disk.h sys/kern/init/kernel_start.h sys/kern/init/sysconfig.h sys/kern/mm/dma_pool.h sys/kern/mm/kmalloc.h sys/kern/mm/memory_boot.h sys/kern/mm/paging.h sys/kern/mm/paging_app.h sys/kern/mm/paging_lowmap.h sys/kern/mm/pgalloc.h sys/kern/mm/physmem.h sys/kern/mm/shlib.h sys/kern/mm/shm.h sys/arch/x86/kernel/double_fault.h sys/arch/x86/kernel/gdt.h sys/arch/x86/kernel/idt.h sys/arch/x86/kernel/mtrr_wc.h sys/arch/x86/kernel/tss.h sys/arch/x86/kernel/x87.h sys/arch/x86/kernel/x87_emu.h sys/arch/x86/kernel/x87_emu_internal.h sys/arch/x86/kernel/x87_nm.h $(wildcard drivers/*.h) sys/dev/ata/atapi.h sys/dev/ata/cd_body.h sys/dev/ata/cd_module.h sys/dev/ata/ide.h sys/dev/ata/ide_addr.h $(wildcard sys/dev/base/*.h) sys/dev/block/disk.h sys/dev/block/fdc.h sys/dev/block/fdc_decide.h sys/dev/block/fdc_track.h sys/dev/block/loop_dev.h sys/dev/block/netcd.h sys/platform/pc98/scsi.h sys/dev/char/np2sysp.h sys/dev/char/rtc.h sys/dev/char/serial.h sys/dev/char/serial_kapi.h sys/dev/char/serial_plan.h sys/dev/dma/dma8237.h sys/platform/pc98/input/kbd.h sys/dev/input/kbd_dlog.h sys/dev/input/kbd_status.h sys/platform/pc98/input/mouse.h sys/dev/net/e82557/e82557.h sys/dev/net/e82557/e82557_desc.h sys/dev/net/e82557/e82557_eeprom.h sys/dev/net/e82557/e82557_regs.h sys/dev/net/e82557/lan_module.h sys/dev/net/ne2000/lgy98.h sys/dev/net/ne2000/ne2000.h sys/dev/net/ne2000/ne2000_regs.h sys/dev/net/ne2000/ne2000_ring.h sys/dev/pci/pci.h sys/dev/pci/pci_barsize.h sys/dev/pci/pci_bind.h sys/dev/pci/pci_decode.h sys/dev/pci/pci_hwb.h sys/dev/pci/pci_locate.h sys/dev/sound/cs4231/pcm_cs4231.h sys/dev/sound/cs4231/pcm_e4_diag.h sys/dev/sound/cs4231/pcm_e4_wire.h sys/dev/sound/cs4231/pcm_hw.h sys/dev/sound/cs4231/pcm_module.h sys/dev/sound/cs4231/pcm_refill.h sys/dev/sound/fm/fm.h sys/dev/sound/fm/fm_module.h sys/dev/sound/fm/opn_chip.h sys/dev/sound/fm/opn_probe.h sys/dev/video/cirrus/cirrus_module.h sys/dev/video/cirrus/wab_cirrus.h sys/dev/video/cirrus/wab_glue.h sys/platform/pc98/video/cirrus/wab_glue_xe10.h sys/dev/video/geforce/geforce_module.h sys/platform/pc98/video/geforce/geforce_tick.h sys/dev/video/geforce/gf_v2.h sys/dev/video/geforce/leave_log.h sys/dev/video/kcg/kcg.h sys/dev/video/kcg/kcg_module.h sys/platform/pc98/video/pc98_route.h sys/platform/pc98/video/splash_raster.h sys/platform/pc98/video/splash_raster_image.h sys/platform/pc98/video/splash_raster_play.h sys/platform/pc98/video/splash_raster_run.h lib/crc32.h sys/kern/kstring.h lib/kutf16.h lib/le_internal.h lib/lz4.h lib/path.h lib/pc98_cell.h lib/serial_diag.h lib/softfloat_bits.h lib/unicode_jis_table.h lib/utf8.h sys/fs/fd_redirect.h sys/fs/ide_limits.h sys/fs/pc98pt.h sys/fs/pipe_buffer.h sys/fs/vfs.h sys/fs/vfs_device.h sys/fs/vfs_stdin.h sys/kern/exec/appmem.h sys/kern/exec/appslot.h sys/kern/exec/exec.h sys/kern/exec/exec_font_guard.h sys/kern/exec/exec_font_lease.h sys/kern/exec/exec_gfx.h sys/kern/exec/exec_gfx_poll.h sys/kern/exec/exec_gui.h sys/kern/exec/exec_heap.h sys/kern/exec/exec_hooks.h sys/kern/exec/exec_hwb.h sys/kern/exec/exec_ime.h sys/kern/exec/exec_kapi_db.h sys/kern/exec/exec_legacy.h sys/kern/exec/exec_r1.h sys/kern/exec/exec_reclaim.h sys/kern/exec/exec_serial_diag.h sys/kern/exec/exec_shlib.h sys/kern/exec/exec_surface.h sys/kern/exec/lease.h sys/kern/exec/os32x_hdr.h sys/kern/exec/owner_diag.h sys/kern/exec/ring3_ls.h sys/kern/exec/ring3_str.h sys/kern/exec/shell_service.h sys/kern/exec/surface_limits.h sys/kern/exec/surface_query.h sys/kern/exec/system_surface.h sys/hal/gfx/gfx.h sys/kern/console/gfx_console.h sys/hal/gfx/gfx_dirty.h sys/hal/gfx/gfx_emergency.h sys/hal/gfx/gfx_hw_mono_cpu.h sys/hal/gfx/gfx_internal.h sys/kern/gfx/osri/gfx_osri.h sys/kern/gfx/osri/gfx_osri_view.h sys/kern/gfx/osri/gfx_osri_view_internal.h sys/hal/gfx/gfx_policy.h sys/kern/gfx/osri/osri_profile.h sys/hal/gfx/palette.h sys/arch/x86/arch_abi.h sys/arch/x86/arch_copy.h sys/arch/x86/arch_cpu.h sys/arch/x86/arch_diag_stack.h sys/arch/x86/arch_endian_dma.h sys/arch/x86/arch_exec_cache.h sys/arch/x86/arch_exec_image.h sys/arch/x86/arch_exec_kapi.h sys/arch/x86/arch_exec_selftest.h sys/arch/x86/arch_exec_storage.h sys/arch/x86/arch_io.h sys/arch/x86/arch_jmp_storage.h sys/arch/x86/arch_kernel_fp.h sys/arch/x86/arch_tick_budget.h sys/arch/x86/arch_type_contract.h sys/arch/x86/x86_desc.h sys/arch/x86/x86_flags.h sys/platform/pc98/bios_snapshot.h sys/platform/pc98/boot.h sys/platform/pc98/boot_disk.h sys/platform/pc98/boot_display.h sys/platform/pc98/boot_names.h sys/platform/pc98/boot_platform.h sys/platform/pc98/boot_splash.h sys/platform/pc98/bootinfo_native.h sys/platform/pc98/bootinfo_wire.h sys/platform/pc98/clock_source.h sys/platform/pc98/display_private.h sys/platform/pc98/install_geometry.h sys/platform/pc98/pci_resources.h sys/platform/pc98/pit.h sys/platform/pc98/pit_math.h sys/platform/pc98/pit_time.h sys/platform/pc98/platform_atapi_bus.h sys/platform/pc98/platform_boot_layout.h sys/platform/pc98/platform_build_info.h sys/platform/pc98/platform_console.h sys/platform/pc98/platform_display_fallback.h sys/platform/pc98/platform_ext2.h sys/platform/pc98/platform_hwb.h sys/platform/pc98/platform_hwb_addr.h sys/platform/pc98/platform_hwb_pci.h sys/platform/pc98/platform_hwb_session.h sys/platform/pc98/platform_ide_bus.h sys/platform/pc98/platform_ide_ports.h sys/platform/pc98/platform_ime_render.h sys/platform/pc98/platform_input_caps.h sys/platform/pc98/platform_install_layout.h sys/platform/pc98/platform_install_management.h sys/platform/pc98/platform_io.h sys/platform/pc98/platform_irq_limits.h sys/platform/pc98/platform_kprintf.h sys/platform/pc98/platform_link_inline.h sys/platform/pc98/platform_mem_stat.h sys/platform/pc98/platform_memory_desc.h sys/platform/pc98/platform_osri.h sys/platform/pc98/platform_paging_window.h sys/platform/pc98/platform_pic.h sys/platform/pc98/platform_usb_identity.h sys/platform/pc98/platform_ver.h sys/platform/pc98/sysclk.h sys/platform/pc98/tick_order.h sys/platform/pc98/tsc_clock.h sys/platform/pc98/tvram_private.h sys/platform/pc98/usb_probe.h sys/platform/pc98/usb_resident.h $(wildcard sdk/include/os32/*.h) sys/platform/pc98/kdebug/crt_diag.h sys/dev/video/geforce/diag/gf5_test.h sys/dev/video/geforce/diag/gf_accel_test.h sys/dev/video/geforce/diag/gf_command_test.h sys/dev/video/geforce/diag/gf_command_wire.h sys/dev/video/geforce/diag/gf_display_test.h sys/dev/video/geforce/diag/gf_foundation_test.h sys/dev/video/geforce/diag/gf_foundation_wire.h sys/dev/video/geforce/diag/gf_prim_bench.h sys/dev/video/geforce/diag/gf_stage_state.h sys/dev/video/geforce/diag/gfinit.h sys/dev/video/geforce/diag/gfinit_bus.h sys/dev/video/geforce/diag/gfinit_decode.h sys/dev/video/geforce/diag/gfinit_exec.h sys/dev/video/geforce/diag/gfinit_plan.h sys/dev/video/geforce/diag/gfinit_plan_c.h sys/dev/video/geforce/diag/gfinit_pll.h sys/dev/video/geforce/diag/gfinit_resources.h sys/dev/video/geforce/diag/gfinit_wire.h sys/kern/kdebug/hwb_wire.h sys/arch/x86/kdebug/kdebug_window.h sys/platform/pc98/kdebug/kpeek.h sys/platform/pc98/kdebug/nic_trace.h sys/kern/kdebug/probe_validate.h sys/kern/kdebug/probe_wire.h sys/dev/video/geforce/diag/recover.h sys/platform/pc98/kdebug/recover_cleanup.h sys/kern/kdebug/recover_diag.h sys/dev/video/geforce/diag/recover_gf.h sys/dev/video/geforce/diag/recover_gf_c.h sys/dev/video/geforce/diag/recover_gf_expand.h sys/kern/kdebug/recover_virtual.h sys/kern/kdebug/recover_wire.h sys/kern/kdebug/selftest_internal.h sys/dev/video/trident/diag/tri_command_wire.h sys/dev/video/trident/diag/trident_command_test.h sys/platform/pc98/kdebug/v86_gcap_defs.h sys/platform/pc98/kdebug/v86_gcap_diag.h sys/dev/video/geforce/gf_load_state.h sys/dev/video/geforce/gfinit_mode.h sys/dev/hwdev/hwdev_module.h sys/dev/hwdev/hwdev_private.h sys/dev/hwdev/owner.h
HWDEV_GENERATED := $(addprefix $(HWDEV_GEN)/,hwdev_contract.h hwdev_core.c hwdev_imports.h hwdev_layout.txt hwdev_import_macros.h hwdev_import_slots.c hwdev_import_stubs.S hwdev_imports.json)
$(HWDEV_GENERATED) &: tools/module_schema.py sys/dev/hwdev/hwdev_imports.def sys/kern/module/kdebug_imports.def tools/gen_kdebug_imports.py build/config.mk build/kdebug.mk tools/gen_hwdev_contract.py tools/hwdev_layout.json docs/design/hwdev/hwdev_imports.md $(HWDEV_HEADERS)
	python3 tools/gen_hwdev_contract.py --target $(ARCH)-$(PLATFORM) --cc $(RAW_CC) --readelf $(READELF) --objcopy $(OBJCOPY) --flags '$(filter-out -MMD -MP,$(KERNEL_CFLAGS))' --includes '$(INC_KERNEL)'
$(KDEBUG_OUT)/hwdev_core.o: $(HWDEV_GEN)/hwdev_core.c $(HWDEV_GEN)/hwdev_contract.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(HWDEV_GEN) -c $< -o $@
$(KDEBUG_OUT)/kdebug_main.o $(KERNEL_OBJ_PREFIX)sys/dev/hwdev/hwdev_module.o $(R1_OBJ)/sys/dev/hwdev/hwdev_module.o: $(HWDEV_GENERATED)
$(KDEBUG_OUT)/kdebug.elf: $(KDEBUG_OUT)/hwdev_core.o

-include $(KDEBUG_OUT)/hwdev_core.d
