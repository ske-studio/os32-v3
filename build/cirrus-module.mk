# T5c-5b: reuse T4 function imports, VA0 packing and paired distribution.
CIRRUS_OUT := $(BUILD_OUT)/cirrus
CIRRUS_GEN := $(addprefix $(CIRRUS_OUT)/,cirrus_imports.h cirrus_imports.json cirrus_import_slots.c cirrus_import_stubs.S cirrus_provider_types.c cirrus_kexports.c)
CIRRUS_DEF_SOURCES := drivers/video/cirrus/cirrus_module.h include/gfx_hal.h include/gfx_mode.h drivers/video/cirrus/wab_glue.h
$(CIRRUS_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/cirrus_imports.def $(CIRRUS_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module cirrus --out-dir $(CIRRUS_OUT)

CIRRUS_OBJS := $(addprefix $(CIRRUS_OUT)/,cirrus_body.o backend_cirrus.o wab_cirrus.o wab_glue_xe10.o cirrus_import_slots.o cirrus_import_stubs.o)
CIRRUS_INC := $(INC_KERNEL) -I. -I$(CIRRUS_OUT)
$(CIRRUS_OUT)/cirrus_body.o: drivers/video/cirrus/cirrus_body.c $(CIRRUS_GEN) build/config.mk build/cirrus-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_CIRRUS_MODULE $(CIRRUS_INC) -c $< -o $@
$(CIRRUS_OUT)/backend_cirrus.o: drivers/video/cirrus/backend_cirrus.c $(CIRRUS_GEN) build/config.mk build/cirrus-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_CIRRUS_MODULE $(CIRRUS_INC) -c $< -o $@
$(CIRRUS_OUT)/wab_cirrus.o $(CIRRUS_OUT)/wab_glue_xe10.o: $(CIRRUS_OUT)/%.o: drivers/video/cirrus/%.c $(CIRRUS_GEN) build/config.mk build/cirrus-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_CIRRUS_MODULE $(CIRRUS_INC) -c $< -o $@
$(CIRRUS_OUT)/cirrus_import_slots.o $(CIRRUS_OUT)/cirrus_provider_types.o $(CIRRUS_OUT)/cirrus_kexports.o: $(CIRRUS_OUT)/%.o: $(CIRRUS_OUT)/%.c $(CIRRUS_GEN) build/config.mk build/cirrus-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(CIRRUS_INC) -c $< -o $@
$(CIRRUS_OUT)/cirrus_import_stubs.o: $(CIRRUS_OUT)/cirrus_import_stubs.S build/config.mk build/cirrus-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(CIRRUS_OUT)/cirrus.elf: $(CIRRUS_OBJS) build/cirrus-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/cirrus-module.ld -Map=$(CIRRUS_OUT)/cirrus.map $(CIRRUS_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(CIRRUS_OUT)/cirrus_kexports.o
R1_OBJECTS += $(CIRRUS_OUT)/cirrus_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(CIRRUS_OUT)/cirrus_kexports.o
$(KERNEL_OBJ_PREFIX)drivers/video/cirrus/cirrus_module.o $(R1_OBJ)/drivers/video/cirrus/cirrus_module.o: $(CIRRUS_GEN)
$(KERNEL_OBJ_PREFIX)drivers/video/cirrus/cirrus_module.o $(R1_OBJ)/drivers/video/cirrus/cirrus_module.o: private CFLAGS_BASE += -I$(CIRRUS_OUT)

define pack_cirrus
	python3 tools/mkmod.py --module-id 10 --elf $(CIRRUS_OUT)/cirrus.elf --metadata $(CIRRUS_OUT)/cirrus_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/cirrus.mod: $(CIRRUS_OUT)/cirrus.elf $(CIRRUS_OUT)/cirrus_imports.json $(CIRRUS_OUT)/cirrus_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/cirrus-module.mk
	$(call pack_cirrus,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/cirrus.mod: $(CIRRUS_OUT)/cirrus.elf $(CIRRUS_OUT)/cirrus_imports.json $(CIRRUS_OUT)/cirrus_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/cirrus-module.mk
	$(call pack_cirrus,$(R1_OUT)/vmkernel.lz4)
-include $(CIRRUS_OBJS:.o=.d) $(CIRRUS_OUT)/cirrus_kexports.d $(CIRRUS_OUT)/cirrus_provider_types.d
