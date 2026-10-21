# M3a: a separate GF body; kdebug retains its existing GF objects until M3c.
HWDEV_OUT := $(BUILD_OUT)/hwdev
HWDEV_GF := gfinit_bus gfinit gfinit_plan gfinit_resources gfinit_decode gfinit_exec gfinit_pll recover_gf
HWDEV_OBJS := $(addprefix $(HWDEV_OUT)/,$(addsuffix .o,$(HWDEV_GF))) \
    $(HWDEV_OUT)/hwdev_main.o $(HWDEV_OUT)/hwdev_import_slots.o $(HWDEV_OUT)/hwdev_import_stubs.o
$(HWDEV_OUT)/%.o: kdebug/%.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -DHWDEV_MODULE -c $< -o $@
$(HWDEV_OUT)/hwdev_pair.h: $(BUILD_OUT)/kdebug.bin $(BUILD_OUT)/vmkernel.lz4 tools/gen_hwdev_pair.py tools/mkmod.py tools/module_schema.py
	python3 tools/gen_hwdev_pair.py --kernel $(BUILD_OUT)/vmkernel.lz4 --core $(BUILD_OUT)/kdebug.bin --out $@
$(HWDEV_OUT)/hwdev_main.o: hwdev/hwdev_main.c $(HWDEV_GENERATED) $(HWDEV_OUT)/hwdev_pair.h build/config.mk build/hwdev.mk
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(HWDEV_OUT) -Werror -c $< -o $@
$(HWDEV_OUT)/hwdev_import_slots.o: $(HWDEV_GEN)/hwdev_import_slots.c $(HWDEV_GENERATED) build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@
$(HWDEV_OUT)/hwdev_import_stubs.o: $(HWDEV_GEN)/hwdev_import_stubs.S build/config.mk build/hwdev.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(HWDEV_OUT)/hwdev.elf: $(HWDEV_OBJS) build/hwdev.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/hwdev.ld -Map=$(HWDEV_OUT)/hwdev.map $(HWDEV_OBJS) -o $@
$(BUILD_OUT)/hwdev.mod: $(HWDEV_OUT)/hwdev.elf $(HWDEV_GEN)/hwdev_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py build/hwdev.mk sdk/kapi.json sdk/include/os32/os32_generations.h
	python3 tools/mkmod.py --module-id 11 --elf $(HWDEV_OUT)/hwdev.elf --metadata $(HWDEV_GEN)/hwdev_imports.json --vmkernel $(BUILD_OUT)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
all: $(BUILD_OUT)/hwdev.mod
-include $(HWDEV_OBJS:.o=.d)
