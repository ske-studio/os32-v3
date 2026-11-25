# GF body lives only in HWDEV; S0 recovery stays in kdebug.
HWDEV_OUT := $(BUILD_OUT)/hwdev
GF_CALC_NAMES := calc timing format pitch start dac_format output cursor fifo extended pll_program
GF_CALC_OBJS := $(addprefix $(HWDEV_OUT)/,$(addsuffix .o,$(GF_CALC_NAMES)))
HWDEV_SHARED := pci_locate recover_gf_locate
HWDEV_GF := gf_load_state gf_stage_state gf_foundation_test gf_command_test gf_accel_test gf_prim_bench gfinit_bus gfinit gfinit_plan gfinit_plan_c gfinit_resources gfinit_decode gfinit_exec gfinit_pll gfinit_mode recover_gf calc timing format pitch start dac_format output cursor fifo extended pll_program
HWDEV_OBJS := $(addprefix $(HWDEV_OUT)/,$(addsuffix .o,$(HWDEV_SHARED) $(HWDEV_GF))) \
    $(HWDEV_OUT)/hwdev_main.o $(HWDEV_OUT)/hwdev_import_slots.o $(HWDEV_OUT)/hwdev_import_stubs.o
$(HWDEV_OUT)/pci_locate.o: sys/dev/pci/pci_locate.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -c $< -o $@
# Normal and R1 share this decoder object; keep all other units at -O2.
$(HWDEV_OUT)/gfinit_decode.o: private KERNEL_CFLAGS += -Os
$(HWDEV_OUT)/%.o: sys/dev/video/geforce/diag/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -c $< -o $@
$(HWDEV_OUT)/hwdev_pair.h: $(BUILD_OUT)/kdebug.bin $(BUILD_OUT)/vmkernel.lz4 tools/gen_hwdev_pair.py tools/mkmod.py sdk/target_profile.py tools/module_schema.py
	python3 tools/gen_hwdev_pair.py --kernel $(BUILD_OUT)/vmkernel.lz4 --core $(BUILD_OUT)/kdebug.bin --out $@
# The embedded manifest binds to the matching kernel/core, including R1.
R1_HWDEV_OUT := $(R1_OUT)/hwdev
$(R1_HWDEV_OUT)/hwdev_pair.h: $(R1_OUT)/kdebug.bin $(R1_OUT)/vmkernel.lz4 tools/gen_hwdev_pair.py tools/mkmod.py sdk/target_profile.py tools/module_schema.py
	python3 tools/gen_hwdev_pair.py --kernel $(R1_OUT)/vmkernel.lz4 --core $(R1_OUT)/kdebug.bin --out $@
$(HWDEV_OUT)/hwdev_main.o: $(HWDEV_OUT)/hwdev_pair.h
$(R1_HWDEV_OUT)/hwdev_main.o: $(R1_HWDEV_OUT)/hwdev_pair.h
$(HWDEV_OUT)/hwdev_main.o $(R1_HWDEV_OUT)/hwdev_main.o: sys/dev/hwdev/hwdev_main.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(@D) -Werror -c $< -o $@
$(HWDEV_OUT)/hwdev_import_slots.o: $(HWDEV_GEN)/hwdev_import_slots.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@
$(HWDEV_OUT)/hwdev_import_stubs.o: $(HWDEV_GEN)/hwdev_import_stubs.S build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(HWDEV_OUT)/hwdev.elf: $(HWDEV_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/hwdev.ld -Map=$(HWDEV_OUT)/hwdev.map $(HWDEV_OBJS) -o $@
R1_HWDEV_OBJS := $(filter-out $(HWDEV_OUT)/hwdev_main.o,$(HWDEV_OBJS)) $(R1_HWDEV_OUT)/hwdev_main.o
$(R1_HWDEV_OUT)/hwdev.elf: $(R1_HWDEV_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/hwdev.ld -Map=$(R1_HWDEV_OUT)/hwdev.map $(R1_HWDEV_OBJS) -o $@
define pack_hwdev
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 11 --elf $(1)/hwdev.elf --metadata $(HWDEV_GEN)/hwdev_imports.json --vmkernel $(2)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(R1_OUT)/hwdev.mod: $(R1_HWDEV_OUT)/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py build/hwdev.mk sdk/kapi.json sdk/include/os32/os32_generations.h
	$(call pack_hwdev,$(R1_HWDEV_OUT),$(R1_OUT))
$(BUILD_OUT)/hwdev.mod: $(HWDEV_OUT)/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py build/hwdev.mk sdk/kapi.json sdk/include/os32/os32_generations.h
	$(call pack_hwdev,$(HWDEV_OUT),$(BUILD_OUT))
all: $(BUILD_OUT)/hwdev.mod
-include $(HWDEV_OBJS:.o=.d) $(R1_HWDEV_OUT)/hwdev_main.d

# Isolated diagnostic pair; it is packed against the normal kernel image.
VPLL_OUT := $(BUILD_OUT)/gf-vpll-probe
VPLL_CORE_OBJS := $(patsubst $(KDEBUG_OUT)/%.o,$(VPLL_OUT)/core/%.o,$(KDEBUG_OBJS)) $(VPLL_OUT)/core/hwdev_core.o
VPLL_HW_OBJS := $(patsubst $(HWDEV_OUT)/%.o,$(VPLL_OUT)/hw/%.o,$(HWDEV_OBJS))
ifeq ($(GF_VPLL_PROBE),1)
.PHONY: gf-vpll-probe vpll-force
vpll-force:
gf-vpll-probe: $(VPLL_OUT)/kdebug.bin $(VPLL_OUT)/hwdev.mod
$(VPLL_OUT)/core/pci_locate.o: sys/dev/pci/pci_locate.c $(KDEBUG_GENERATED) $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/pci_locate.o: sys/dev/pci/pci_locate.c $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/core/%.o: sys/dev/video/geforce/diag/%.c $(KDEBUG_GENERATED) $(HWDEV_GENERATED) build/kdebug.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DGF_VPLL_PROBE=1 $(if $(filter recover_profile,$*),-DRECOVER_MODULE) -c $< -o $@
$(VPLL_OUT)/core/%.o: $(KDEBUG_GEN)/%.c $(KDEBUG_GENERATED) build/kdebug.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(KDEBUG_GEN) -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/core/kdebug_import_stubs.o: $(KDEBUG_GEN)/kdebug_import_stubs.S $(KDEBUG_GENERATED)
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(VPLL_OUT)/core/hwdev_core.o: $(HWDEV_GEN)/hwdev_core.c $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(HWDEV_GEN) -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/core/kdebug.elf: $(VPLL_CORE_OBJS) build/module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -Map=$(VPLL_OUT)/core/kdebug.map -T build/module.ld $(VPLL_CORE_OBJS) -o $@
$(VPLL_OUT)/kdebug.bin: $(VPLL_OUT)/core/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json tools/mkmod.py vpll-force
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --elf $< --metadata $(KDEBUG_GEN)/kdebug_imports.json --vmkernel $(BUILD_OUT)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
$(VPLL_OUT)/hw/%.o: sys/dev/video/geforce/diag/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(if $(filter gfinit_decode,$*),-Os) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_pair.h: $(VPLL_OUT)/kdebug.bin tools/gen_hwdev_pair.py
	mkdir -p $(@D)
	python3 tools/gen_hwdev_pair.py --kernel $(BUILD_OUT)/vmkernel.lz4 --core $(VPLL_OUT)/kdebug.bin --out $@
$(VPLL_OUT)/hw/hwdev_main.o: sys/dev/hwdev/hwdev_main.c $(VPLL_OUT)/hw/hwdev_pair.h $(HWDEV_GENERATED)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(@D) -Werror -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_import_slots.o: $(HWDEV_GEN)/hwdev_import_slots.c $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_import_stubs.o: $(HWDEV_GEN)/hwdev_import_stubs.S $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(VPLL_OUT)/hw/hwdev.elf: $(VPLL_HW_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/hwdev.ld -Map=$(VPLL_OUT)/hw/hwdev.map $(VPLL_HW_OBJS) -o $@
$(VPLL_OUT)/hwdev.mod: $(VPLL_OUT)/hw/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json tools/mkmod.py
	$(call pack_hwdev,$(VPLL_OUT)/hw,$(BUILD_OUT))
-include $(VPLL_CORE_OBJS:.o=.d) $(VPLL_HW_OBJS:.o=.d)
else
.PHONY: gf-vpll-probe
gf-vpll-probe:
	@echo 'Use make GF_VPLL_PROBE=1 gf-vpll-probe' >&2; exit 1
endif

$(HWDEV_OUT)/gf_load_state.o: sys/dev/video/geforce/gf_load_state.c sys/dev/video/geforce/gf_load_state.h sys/dev/video/geforce/gf_mode_rows.inc build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -c $< -o $@
$(VPLL_OUT)/hw/gf_load_state.o: sys/dev/video/geforce/gf_load_state.c sys/dev/video/geforce/gf_load_state.h sys/dev/video/geforce/gf_mode_rows.inc build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -c $< -o $@

# Shared init components are linked into the diagnostic HWDEV module.
GF_INIT_SOURCES := sys/dev/video/geforce/runner.c sys/dev/video/geforce/save.c sys/dev/video/geforce/pll.c sys/dev/video/geforce/op/op/compute_mem.c sys/dev/video/geforce/op/op/condition.c sys/dev/video/geforce/op/op/copy.c sys/dev/video/geforce/op/op/copy_nv_reg.c sys/dev/video/geforce/op/op/cr.c sys/dev/video/geforce/op/op/cr_index_address_latched.c sys/dev/video/geforce/op/op/done.c sys/dev/video/geforce/op/op/end_repeat.c sys/dev/video/geforce/op/op/index_io.c sys/dev/video/geforce/op/op/io_flag_condition.c sys/dev/video/geforce/op/op/io_restrict_pll2.c sys/dev/video/geforce/op/op/io_restrict_prog.c sys/dev/video/geforce/op/op/macro.c sys/dev/video/geforce/op/op/not.c sys/dev/video/geforce/op/op/nv_reg.c sys/dev/video/geforce/op/op/pll2.c sys/dev/video/geforce/op/op/repeat.c sys/dev/video/geforce/op/op/reset.c sys/dev/video/geforce/op/op/resume.c sys/dev/video/geforce/op/op/sub.c sys/dev/video/geforce/op/op/sub_direct.c sys/dev/video/geforce/op/op/time.c sys/dev/video/geforce/op/op/tmds_reg.c sys/dev/video/geforce/op/op/zm_cr.c sys/dev/video/geforce/op/op/zm_cr_group.c sys/dev/video/geforce/op/op/zm_index_io.c sys/dev/video/geforce/op/op/zm_reg.c sys/dev/video/geforce/op/op/zm_reg_sequence.c sys/dev/video/geforce/op/op/zm_tmds_group.c
GF_INIT_OBJS := $(patsubst sys/dev/video/geforce/%.c,$(HWDEV_OUT)/gf/%.o,$(subst /op/op/,/op/,$(GF_INIT_SOURCES)))
$(HWDEV_OUT)/gf/op/%.o: sys/dev/video/geforce/op/op/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -Werror -c $< -o $@
$(HWDEV_OUT)/gf/%.o: sys/dev/video/geforce/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -Werror -c $< -o $@
# Diagnostics now execute the same A/main runner as production.
HWDEV_OBJS += $(GF_INIT_OBJS)
R1_HWDEV_OBJS += $(GF_INIT_OBJS)
VPLL_GF_INIT_OBJS := $(patsubst $(HWDEV_OUT)/%,$(VPLL_OUT)/hw/%,$(GF_INIT_OBJS))
VPLL_HW_OBJS += $(VPLL_GF_INIT_OBJS)
$(VPLL_OUT)/hw/gf/op/%.o: sys/dev/video/geforce/op/op/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -Werror -c $< -o $@
$(VPLL_OUT)/hw/gf/%.o: sys/dev/video/geforce/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -Werror -c $< -o $@
$(HWDEV_OUT)/hwdev.elf $(R1_HWDEV_OUT)/hwdev.elf: $(GF_INIT_OBJS)
$(VPLL_OUT)/hw/hwdev.elf: $(VPLL_GF_INIT_OBJS)
-include $(GF_INIT_OBJS:.o=.d)

# P3 calculation is linked once; diagnostic C and production use its functions.
$(GF_CALC_OBJS): $(HWDEV_OUT)/%.o: sys/dev/video/geforce/display/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -Werror -c $< -o $@
# F4 transaction/lifecycle and new MMIO components belong only to geforce.mod.
GF_MODULE_DISPLAY_NAMES := dcb ddc dpms flip gamma tmds tv heads transaction service save recover scan links hw_state hw_state_crtc hw_fonts hw_load hw_pll enc_state
GF_DISPLAY_SOURCES := sys/dev/video/geforce/service.c $(filter-out $(addprefix sys/dev/video/geforce/display/,$(addsuffix .c,$(GF_CALC_NAMES) $(GF_MODULE_DISPLAY_NAMES))),sys/dev/video/geforce/display/baseline.c sys/dev/video/geforce/display/blank.c sys/dev/video/geforce/display/calc.c sys/dev/video/geforce/display/checks.c sys/dev/video/geforce/display/cursor.c sys/dev/video/geforce/display/dac_format.c sys/dev/video/geforce/display/dcb.c sys/dev/video/geforce/display/ddc.c sys/dev/video/geforce/display/dpms.c sys/dev/video/geforce/display/enc_state.c sys/dev/video/geforce/display/extended.c sys/dev/video/geforce/display/fifo.c sys/dev/video/geforce/display/flip.c sys/dev/video/geforce/display/format.c sys/dev/video/geforce/display/gamma.c sys/dev/video/geforce/display/heads.c sys/dev/video/geforce/display/hw_fonts.c sys/dev/video/geforce/display/hw_load.c sys/dev/video/geforce/display/hw_pll.c sys/dev/video/geforce/display/hw_state.c sys/dev/video/geforce/display/hw_state_crtc.c sys/dev/video/geforce/display/links.c sys/dev/video/geforce/display/load_row.c sys/dev/video/geforce/display/modes.c sys/dev/video/geforce/display/modeset.c sys/dev/video/geforce/display/output.c sys/dev/video/geforce/display/palette.c sys/dev/video/geforce/display/pitch.c sys/dev/video/geforce/display/pll_program.c sys/dev/video/geforce/display/quiesce.c sys/dev/video/geforce/display/recover.c sys/dev/video/geforce/display/save.c sys/dev/video/geforce/display/scan.c sys/dev/video/geforce/display/service.c sys/dev/video/geforce/display/start.c sys/dev/video/geforce/display/timing.c sys/dev/video/geforce/display/tmds.c sys/dev/video/geforce/display/transaction.c sys/dev/video/geforce/display/tv.c)
GF_DISPLAY_OBJS := $(patsubst sys/dev/video/geforce/%.c,$(HWDEV_OUT)/gf/%.o,$(GF_DISPLAY_SOURCES))
$(HWDEV_OUT)/hwdev.elf: $(GF_DISPLAY_OBJS)
-include $(GF_CALC_OBJS:.o=.d) $(GF_DISPLAY_OBJS:.o=.d)
$(addprefix $(VPLL_OUT)/hw/,$(addsuffix .o,$(GF_CALC_NAMES))): $(VPLL_OUT)/hw/%.o: sys/dev/video/geforce/display/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -Werror -c $< -o $@
