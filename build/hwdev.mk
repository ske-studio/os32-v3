# GF body lives only in HWDEV; S0 recovery stays in kdebug.
HWDEV_OUT := $(BUILD_OUT)/hwdev
HWDEV_GF := gfinit_bus gfinit gfinit_plan gfinit_resources gfinit_decode gfinit_exec gfinit_pll recover_gf
HWDEV_OBJS := $(addprefix $(HWDEV_OUT)/,$(addsuffix .o,$(HWDEV_GF))) \
    $(HWDEV_OUT)/hwdev_main.o $(HWDEV_OUT)/hwdev_import_slots.o $(HWDEV_OUT)/hwdev_import_stubs.o
# Normal and R1 share this decoder object; keep all other units at -O2.
$(HWDEV_OUT)/gfinit_decode.o: private KERNEL_CFLAGS += -Os
$(HWDEV_OUT)/%.o: kdebug/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -c $< -o $@
$(HWDEV_OUT)/hwdev_pair.h: $(BUILD_OUT)/kdebug.bin $(BUILD_OUT)/vmkernel.lz4 tools/gen_hwdev_pair.py tools/mkmod.py tools/module_schema.py
	python3 tools/gen_hwdev_pair.py --kernel $(BUILD_OUT)/vmkernel.lz4 --core $(BUILD_OUT)/kdebug.bin --out $@
# The embedded manifest binds to the matching kernel/core, including R1.
R1_HWDEV_OUT := $(R1_OUT)/hwdev
$(R1_HWDEV_OUT)/hwdev_pair.h: $(R1_OUT)/kdebug.bin $(R1_OUT)/vmkernel.lz4 tools/gen_hwdev_pair.py tools/mkmod.py tools/module_schema.py
	python3 tools/gen_hwdev_pair.py --kernel $(R1_OUT)/vmkernel.lz4 --core $(R1_OUT)/kdebug.bin --out $@
$(HWDEV_OUT)/hwdev_main.o: $(HWDEV_OUT)/hwdev_pair.h
$(R1_HWDEV_OUT)/hwdev_main.o: $(R1_HWDEV_OUT)/hwdev_pair.h
$(HWDEV_OUT)/hwdev_main.o $(R1_HWDEV_OUT)/hwdev_main.o: hwdev/hwdev_main.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(@D) -Werror -c $< -o $@
$(HWDEV_OUT)/hwdev_import_slots.o: $(HWDEV_GEN)/hwdev_import_slots.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@
$(HWDEV_OUT)/hwdev_import_stubs.o: $(HWDEV_GEN)/hwdev_import_stubs.S build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(HWDEV_OUT)/hwdev.elf: $(HWDEV_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/hwdev.ld -Map=$(HWDEV_OUT)/hwdev.map $(HWDEV_OBJS) -o $@
R1_HWDEV_OBJS := $(filter-out $(HWDEV_OUT)/hwdev_main.o,$(HWDEV_OBJS)) $(R1_HWDEV_OUT)/hwdev_main.o
$(R1_HWDEV_OUT)/hwdev.elf: $(R1_HWDEV_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/hwdev.ld -Map=$(R1_HWDEV_OUT)/hwdev.map $(R1_HWDEV_OBJS) -o $@
define pack_hwdev
	python3 tools/mkmod.py --module-id 11 --elf $(1)/hwdev.elf --metadata $(HWDEV_GEN)/hwdev_imports.json --vmkernel $(2)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(R1_OUT)/hwdev.mod: $(R1_HWDEV_OUT)/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py build/hwdev.mk sdk/kapi.json sdk/include/os32/os32_generations.h
	$(call pack_hwdev,$(R1_HWDEV_OUT),$(R1_OUT))
$(BUILD_OUT)/hwdev.mod: $(HWDEV_OUT)/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py build/hwdev.mk sdk/kapi.json sdk/include/os32/os32_generations.h
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
$(VPLL_OUT)/core/%.o: kdebug/%.c $(KDEBUG_GENERATED) $(HWDEV_GENERATED) build/kdebug.mk build/hwdev.mk
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
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -Map=$(VPLL_OUT)/core/kdebug.map -T build/module.ld $(VPLL_CORE_OBJS) -o $@
$(VPLL_OUT)/kdebug.bin: $(VPLL_OUT)/core/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json tools/mkmod.py vpll-force
	python3 tools/mkmod.py --elf $< --metadata $(KDEBUG_GEN)/kdebug_imports.json --vmkernel $(BUILD_OUT)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
$(VPLL_OUT)/hw/%.o: kdebug/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(if $(filter gfinit_decode,$*),-Os) $(INC_KERNEL) -DHWDEV_MODULE -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_pair.h: $(VPLL_OUT)/kdebug.bin tools/gen_hwdev_pair.py
	mkdir -p $(@D)
	python3 tools/gen_hwdev_pair.py --kernel $(BUILD_OUT)/vmkernel.lz4 --core $(VPLL_OUT)/kdebug.bin --out $@
$(VPLL_OUT)/hw/hwdev_main.o: hwdev/hwdev_main.c $(VPLL_OUT)/hw/hwdev_pair.h $(HWDEV_GENERATED)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(@D) -Werror -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_import_slots.o: $(HWDEV_GEN)/hwdev_import_slots.c $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DGF_VPLL_PROBE=1 -c $< -o $@
$(VPLL_OUT)/hw/hwdev_import_stubs.o: $(HWDEV_GEN)/hwdev_import_stubs.S $(HWDEV_GENERATED)
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(VPLL_OUT)/hw/hwdev.elf: $(VPLL_HW_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/hwdev.ld -Map=$(VPLL_OUT)/hw/hwdev.map $(VPLL_HW_OBJS) -o $@
$(VPLL_OUT)/hwdev.mod: $(VPLL_OUT)/hw/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json tools/mkmod.py
	$(call pack_hwdev,$(VPLL_OUT)/hw,$(BUILD_OUT))
-include $(VPLL_CORE_OBJS:.o=.d) $(VPLL_HW_OBJS:.o=.d)
else
.PHONY: gf-vpll-probe
gf-vpll-probe:
	@echo 'Use make GF_VPLL_PROBE=1 gf-vpll-probe' >&2; exit 1
endif
