# T5c-5b: reuse T4 function imports, VA0 packing and paired distribution.
GEFORCE_OUT := $(BUILD_OUT)/geforce
GEFORCE_GEN := $(addprefix $(GEFORCE_OUT)/,geforce_imports.h geforce_imports.json geforce_import_slots.c geforce_import_stubs.S geforce_provider_types.c geforce_kexports.c)
GEFORCE_DEF_SOURCES := sys/hal/gfx/gfx_policy.h sys/kern/gfx/osri/gfx_osri_view.h sys/include/debug_probe.h sys/dev/video/geforce/command.h sys/dev/video/geforce/gf1_test.h sys/dev/video/geforce/gf2_test.h sys/dev/video/geforce/diag/gfinit_bus.h sys/arch/x86/arch_io.h sys/include/pci_contract.h sys/include/pci_bind_contract.h sys/dev/pci/pci.h sys/dev/video/geforce/geforce_module.h sys/include/geforce_contract.h sys/platform/pc98/video/geforce/geforce_tick.h sys/platform/pc98/video/pc98_route.h sys/hal/gfx/palette.h sys/dev/video/geforce/foundation.h sys/dev/video/geforce/internal.h sys/dev/video/geforce/production.h sys/include/gfx_hal.h sys/hal/gfx/gfx_hal2.h sys/include/gfx_mode.h sys/include/io.h sys/include/kprintf.h sys/include/sys.h sys/include/video_service.h sys/kern/irq/irq.h sys/kern/irq/tick.h sys/kern/kstring.h lib/serial_diag.h sys/kern/mm/paging.h sys/kern/mm/pgalloc.h
$(GEFORCE_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py tools/prototypes/geforce_imports.def $(GEFORCE_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module geforce --out-dir $(GEFORCE_OUT)

GEFORCE_OBJS := $(addprefix $(GEFORCE_OUT)/,geforce_body.o backend_geforce.o gf_present.o gf_hook.o gf_direct.o gf_present_m2mf.o $(notdir $(patsubst %.c,%.o,sys/dev/video/geforce/gf_v2_alloc.c sys/dev/video/geforce/gf_v2_display.c sys/dev/video/geforce/gf_v2_enum.c sys/dev/video/geforce/gf_v2_fence.c sys/dev/video/geforce/gf_v2_gart.c sys/dev/video/geforce/gf_v2_life.c sys/dev/video/geforce/gf_v2_ops.c sys/dev/video/geforce/gf_v2_weak.c)) geforce_import_slots.o geforce_import_stubs.o)
GEFORCE_INC := $(INC_KERNEL) -I. -I$(GEFORCE_OUT)
$(GEFORCE_OUT)/geforce_body.o: sys/dev/video/geforce/geforce_body.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/backend_geforce.o: sys/dev/video/geforce/backend_geforce.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/gf_present.o: sys/dev/video/geforce/gf_present.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(patsubst sys/dev/video/geforce/%.c,$(GEFORCE_OUT)/%.o,sys/dev/video/geforce/gf_v2_alloc.c sys/dev/video/geforce/gf_v2_display.c sys/dev/video/geforce/gf_v2_enum.c sys/dev/video/geforce/gf_v2_fence.c sys/dev/video/geforce/gf_v2_gart.c sys/dev/video/geforce/gf_v2_life.c sys/dev/video/geforce/gf_v2_ops.c sys/dev/video/geforce/gf_v2_weak.c) $(GEFORCE_OUT)/gf_hook.o $(GEFORCE_OUT)/gf_direct.o $(GEFORCE_OUT)/gf_present_m2mf.o: $(GEFORCE_OUT)/%.o: sys/dev/video/geforce/%.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
# P3 service and P2 runner are linked independently of diagnostic HWDEV/KDEBUG.
GEFORCE_LAYER2_SOURCES := sys/dev/video/geforce/fifo.c sys/dev/video/geforce/push.c sys/dev/video/geforce/dmaobj.c sys/dev/video/geforce/ramht.c sys/dev/video/geforce/grctx.c sys/dev/video/geforce/gr.c sys/dev/video/geforce/fence.c sys/dev/video/geforce/fault.c sys/dev/video/geforce/command_platform.c sys/dev/video/geforce/command.c
GEFORCE_DISPLAY_STATE_SOURCES := sys/dev/video/geforce/display/hw_state.c sys/dev/video/geforce/display/hw_state_crtc.c sys/dev/video/geforce/display/hw_fonts.c sys/dev/video/geforce/display/hw_load.c sys/dev/video/geforce/display/hw_pll.c sys/dev/video/geforce/display/enc_state.c
GEFORCE_SOURCES := sys/dev/pci/pci_locate.c sys/dev/video/geforce/diag/recover_gf_locate.c  $(filter-out $(GEFORCE_LAYER2_SOURCES),sys/dev/video/geforce/accel_copy.c sys/dev/video/geforce/accel_fill.c sys/dev/video/geforce/accel_ifc.c sys/dev/video/geforce/accel_m2mf.c sys/dev/video/geforce/accel_mono.c sys/dev/video/geforce/accel_objects.c sys/dev/video/geforce/accel_rop.c sys/dev/video/geforce/accel_rop3.c sys/dev/video/geforce/accel_scale.c sys/dev/video/geforce/accel_surface.c sys/dev/video/geforce/accel_transfer.c sys/dev/video/geforce/bus.c sys/dev/video/geforce/clock.c sys/dev/video/geforce/command.c sys/dev/video/geforce/command_platform.c sys/dev/video/geforce/command_test.c sys/dev/video/geforce/devinit.c sys/dev/video/geforce/dmaobj.c sys/dev/video/geforce/engine_stop.c sys/dev/video/geforce/fault.c sys/dev/video/geforce/feature5_links.c sys/dev/video/geforce/feature5_rom.c sys/dev/video/geforce/feature5_service.c sys/dev/video/geforce/fence.c sys/dev/video/geforce/fifo.c sys/dev/video/geforce/foundation.c sys/dev/video/geforce/foundation_test.c sys/dev/video/geforce/gr.c sys/dev/video/geforce/grctx.c sys/dev/video/geforce/instmem.c sys/dev/video/geforce/instmem_map.c sys/dev/video/geforce/irq.c sys/dev/video/geforce/master.c sys/dev/video/geforce/mc.c sys/dev/video/geforce/native.c sys/dev/video/geforce/perf.c sys/dev/video/geforce/pll.c sys/dev/video/geforce/push.c sys/dev/video/geforce/ramclock.c sys/dev/video/geforce/ramht.c sys/platform/pc98/geforce/resources.c sys/dev/video/geforce/runner.c sys/dev/video/geforce/save.c sys/dev/video/geforce/service.c sys/dev/video/geforce/state.c sys/dev/video/geforce/therm.c sys/dev/video/geforce/tile.c sys/dev/video/geforce/volt.c sys/dev/video/geforce/vram_view.c) $(GEFORCE_LAYER2_SOURCES) $(filter-out $(GEFORCE_DISPLAY_STATE_SOURCES),sys/dev/video/geforce/display/baseline.c sys/dev/video/geforce/display/blank.c sys/dev/video/geforce/display/calc.c sys/dev/video/geforce/display/checks.c sys/dev/video/geforce/display/cursor.c sys/dev/video/geforce/display/dac_format.c sys/dev/video/geforce/display/dcb.c sys/dev/video/geforce/display/ddc.c sys/dev/video/geforce/display/dpms.c sys/dev/video/geforce/display/enc_state.c sys/dev/video/geforce/display/extended.c sys/dev/video/geforce/display/fifo.c sys/dev/video/geforce/display/flip.c sys/dev/video/geforce/display/format.c sys/dev/video/geforce/display/gamma.c sys/dev/video/geforce/display/heads.c sys/dev/video/geforce/display/hw_fonts.c sys/dev/video/geforce/display/hw_load.c sys/dev/video/geforce/display/hw_pll.c sys/dev/video/geforce/display/hw_state.c sys/dev/video/geforce/display/hw_state_crtc.c sys/dev/video/geforce/display/links.c sys/dev/video/geforce/display/load_row.c sys/dev/video/geforce/display/modes.c sys/dev/video/geforce/display/modeset.c sys/dev/video/geforce/display/output.c sys/dev/video/geforce/display/palette.c sys/dev/video/geforce/display/pitch.c sys/dev/video/geforce/display/pll_program.c sys/dev/video/geforce/display/quiesce.c sys/dev/video/geforce/display/recover.c sys/dev/video/geforce/display/save.c sys/dev/video/geforce/display/scan.c sys/dev/video/geforce/display/service.c sys/dev/video/geforce/display/start.c sys/dev/video/geforce/display/timing.c sys/dev/video/geforce/display/tmds.c sys/dev/video/geforce/display/transaction.c sys/dev/video/geforce/display/tv.c sys/dev/video/geforce/op/op/compute_mem.c sys/dev/video/geforce/op/op/condition.c sys/dev/video/geforce/op/op/copy.c sys/dev/video/geforce/op/op/copy_nv_reg.c sys/dev/video/geforce/op/op/cr.c sys/dev/video/geforce/op/op/cr_index_address_latched.c sys/dev/video/geforce/op/op/done.c sys/dev/video/geforce/op/op/end_repeat.c sys/dev/video/geforce/op/op/index_io.c sys/dev/video/geforce/op/op/io_flag_condition.c sys/dev/video/geforce/op/op/io_restrict_pll2.c sys/dev/video/geforce/op/op/io_restrict_prog.c sys/dev/video/geforce/op/op/macro.c sys/dev/video/geforce/op/op/not.c sys/dev/video/geforce/op/op/nv_reg.c sys/dev/video/geforce/op/op/pll2.c sys/dev/video/geforce/op/op/repeat.c sys/dev/video/geforce/op/op/reset.c sys/dev/video/geforce/op/op/resume.c sys/dev/video/geforce/op/op/sub.c sys/dev/video/geforce/op/op/sub_direct.c sys/dev/video/geforce/op/op/time.c sys/dev/video/geforce/op/op/tmds_reg.c sys/dev/video/geforce/op/op/zm_cr.c sys/dev/video/geforce/op/op/zm_cr_group.c sys/dev/video/geforce/op/op/zm_index_io.c sys/dev/video/geforce/op/op/zm_reg.c sys/dev/video/geforce/op/op/zm_reg_sequence.c sys/dev/video/geforce/op/op/zm_tmds_group.c) $(GEFORCE_DISPLAY_STATE_SOURCES) sys/dev/video/geforce/gf_load_state.c sys/dev/video/geforce/diag/gfinit_decode.c sys/dev/video/geforce/diag/gfinit_pll.c sys/dev/video/geforce/diag/gfinit_resources.c
GEFORCE_SERVICE_OBJS := $(patsubst %.c,$(GEFORCE_OUT)/%.o,$(GEFORCE_SOURCES))
$(GEFORCE_SERVICE_OBJS): $(GEFORCE_OUT)/%.o: %.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION_MODULE -DGF_PRODUCTION -DGF_PRODUCTION_DECODE $(GEFORCE_INC) -c $< -o $@
GEFORCE_OBJS += $(GEFORCE_SERVICE_OBJS)
$(GEFORCE_OUT)/geforce_import_slots.o $(GEFORCE_OUT)/geforce_provider_types.o $(GEFORCE_OUT)/geforce_kexports.o: $(GEFORCE_OUT)/%.o: $(GEFORCE_OUT)/%.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/geforce_import_stubs.o: $(GEFORCE_OUT)/geforce_import_stubs.S build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(GEFORCE_OUT)/geforce.elf: $(GEFORCE_OBJS) build/geforce-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/geforce-module.ld -Map=$(GEFORCE_OUT)/geforce.map $(GEFORCE_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(GEFORCE_OUT)/geforce_kexports.o
R1_OBJECTS += $(GEFORCE_OUT)/geforce_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(GEFORCE_OUT)/geforce_kexports.o
$(KERNEL_OBJ_PREFIX)sys/dev/video/geforce/geforce_module.o $(R1_OBJ)/sys/dev/video/geforce/geforce_module.o: $(GEFORCE_GEN)
$(KERNEL_OBJ_PREFIX)sys/dev/video/geforce/geforce_module.o $(R1_OBJ)/sys/dev/video/geforce/geforce_module.o: private CFLAGS_BASE += -I$(GEFORCE_OUT)

# mkmod reads the image/mem/load-span limits from tools/module_schema.py (HAL v2 I3).
define pack_geforce
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 12 --elf $(GEFORCE_OUT)/geforce.elf --metadata $(GEFORCE_OUT)/geforce_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(R1_OUT)/vmkernel.lz4)
-include $(GEFORCE_OBJS:.o=.d) $(GEFORCE_OUT)/geforce_kexports.d $(GEFORCE_OUT)/geforce_provider_types.d

# The DMA adapter stays resident and is shared through its typed import.
GEFORCE_DMA_OBJ := sys/dev/video/geforce/geforce_dma.o
GEFORCE_DMA_KERNEL_OBJ := $(addprefix $(KERNEL_OBJ_PREFIX),$(GEFORCE_DMA_OBJ))
GEFORCE_DMA_R1_OBJ := $(addprefix $(R1_OBJ)/,$(GEFORCE_DMA_OBJ))
C_KERNEL_OBJ += $(GEFORCE_DMA_KERNEL_OBJ)
R1_OBJECTS += $(GEFORCE_DMA_R1_OBJ)
$(BUILD_OUT)/kernel.elf: $(GEFORCE_DMA_KERNEL_OBJ)
$(R1_OUT)/kernel.elf: $(GEFORCE_DMA_R1_OBJ)
$(GEFORCE_DMA_KERNEL_OBJ) $(GEFORCE_DMA_R1_OBJ): $(LAYOUT_HEADER) build/kernel.mk build/config.mk
-include $(GEFORCE_DMA_KERNEL_OBJ:.o=.d) $(GEFORCE_DMA_R1_OBJ:.o=.d)

# Identification is also needed before module loading / ledger freeze.
GEFORCE_BOOT_OBJS := $(GEFORCE_OUT)/boot_locate.o $(GEFORCE_OUT)/boot_pci_locate.o  $(GEFORCE_OUT)/boot_resources.o $(GEFORCE_OUT)/boot_scan.o $(GEFORCE_OUT)/geforce_tick.o
$(GEFORCE_OUT)/geforce_tick.o: sys/platform/pc98/video/geforce/geforce_tick.c sys/platform/pc98/video/geforce/geforce_tick.h build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_resources.o: sys/platform/pc98/geforce/resources.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_scan.o: sys/dev/video/geforce/diag/gfinit_resources.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_locate.o: sys/dev/video/geforce/diag/recover_gf_locate.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_pci_locate.o: sys/dev/pci/pci_locate.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
C_KERNEL_OBJ += $(GEFORCE_BOOT_OBJS)
R1_OBJECTS += $(GEFORCE_BOOT_OBJS)
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(GEFORCE_BOOT_OBJS)
-include $(GEFORCE_BOOT_OBJS:.o=.d)

# Explicit acceptance target; production objects/images never receive these flags.
# GF_ACCEPT_ID identifies the paired module + gshell measurement build.
GEFORCE_ACCEPT ?=
ifneq ($(GEFORCE_ACCEPT),)
ifeq ($(filter $(GEFORCE_ACCEPT),byte-uc dword-uc dword-wc),)
$(error GEFORCE_ACCEPT must be byte-uc, dword-uc or dword-wc)
endif
ifeq ($(GF_ACCEPT_ID),)
$(error GF_ACCEPT_ID is required for acceptance builds)
endif
GFA_OUT := $(BUILD_OUT)/geforce-accept/$(GEFORCE_ACCEPT)
GFA_MODE := $(if $(filter byte-uc,$(GEFORCE_ACCEPT)),0,$(if $(filter dword-uc,$(GEFORCE_ACCEPT)),1,2))
GFA_OBJS := $(filter-out $(GEFORCE_OUT)/backend_geforce.o $(GEFORCE_OUT)/gf_present.o,$(GEFORCE_OBJS)) $(addprefix $(GFA_OUT)/,backend_geforce.o gf_present.o gf_accept.o gf_accept_cpu.o)
$(GFA_OUT)/build-id: FORCE
	@mkdir -p $(@D)
	@if [ "$$(cat $@ 2>/dev/null)" != '$(GF_ACCEPT_ID)' ]; then echo '$(GF_ACCEPT_ID)' > $@; fi
$(GFA_OUT)/backend_geforce.o: private GFA_FLAGS = -Dgf_present_rect=gf_accept_transfer
$(GFA_OUT)/gf_present.o: private GFA_FLAGS = $(if $(filter byte-uc,$(GEFORCE_ACCEPT)),-DGF_PRESENT_BYTE)
$(GFA_OUT)/gf_accept_cpu.o: arch/$(ARCH)/video/gf_accept_cpu.c $(GEFORCE_GEN) $(GFA_OUT)/build-id build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE -DGF_ACCEPT_MODE=$(GFA_MODE) -DGF_ACCEPT_ID='"$(GF_ACCEPT_ID)"' $(GEFORCE_INC) -c $< -o $@
$(GFA_OUT)/gf_accept.o: private GFA_FLAGS = -DGF_ACCEPT_MODE=$(GFA_MODE) -DGF_ACCEPT_ID='"$(GF_ACCEPT_ID)"' -DGF_ACCEPT_PAIR=$$(python3 -c 'import sys; from pathlib import Path; sys.path.insert(0,"tools"); import mkmod; print(mkmod.pair_crc(Path("$(BUILD_OUT)/vmkernel.lz4").read_bytes()))')
$(GFA_OUT)/gf_accept.o: $(BUILD_OUT)/vmkernel.lz4
$(GFA_OUT)/%.o: sys/dev/video/geforce/%.c $(GEFORCE_GEN) $(GFA_OUT)/build-id build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GFA_FLAGS) $(GEFORCE_INC) -c $< -o $@
$(GFA_OUT)/geforce.elf: $(GFA_OBJS) build/geforce-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/geforce-module.ld -Map=$(GFA_OUT)/geforce.map $(GFA_OBJS) -o $@
$(GFA_OUT)/geforce.mod: $(GFA_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 12 --elf $(GFA_OUT)/geforce.elf --metadata $(GEFORCE_OUT)/geforce_imports.json --vmkernel $(BUILD_OUT)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
.PHONY: geforce-accept
geforce-accept: $(GFA_OUT)/geforce.mod
-include $(GFA_OBJS:.o=.d)
endif
