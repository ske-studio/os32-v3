# T5c-2a: reuse T4 function imports, VA0 packing and paired distribution.
FM_OUT := $(BUILD_OUT)/fm
FM_GEN := $(addprefix $(FM_OUT)/,fm_imports.h fm_imports.json fm_import_slots.c fm_import_stubs.S fm_provider_types.c fm_kexports.c)
FM_DEF_SOURCES := kernel/fm_module.h drivers/fm.h kernel/snd_engine.h
$(FM_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/fm_imports.def $(FM_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module fm --out-dir $(FM_OUT)

FM_OBJS := $(addprefix $(FM_OUT)/,fm_body.o fm.o snd_engine.o fm_import_slots.o fm_import_stubs.o)
FM_INC := $(INC_KERNEL) -I. -I$(FM_OUT)
$(FM_OUT)/fm_body.o: kernel/fm_body.c $(FM_GEN) build/config.mk build/fm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FM_MODULE $(FM_INC) -c $< -o $@
$(FM_OUT)/fm.o: drivers/fm.c $(FM_GEN) build/config.mk build/fm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FM_MODULE $(FM_INC) -c $< -o $@
$(FM_OUT)/snd_engine.o: kernel/snd_engine.c $(FM_GEN) build/config.mk build/fm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FM_MODULE $(FM_INC) -c $< -o $@
$(FM_OUT)/fm_import_slots.o $(FM_OUT)/fm_provider_types.o $(FM_OUT)/fm_kexports.o: $(FM_OUT)/%.o: $(FM_OUT)/%.c $(FM_GEN) build/config.mk build/fm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(FM_INC) -c $< -o $@
$(FM_OUT)/fm_import_stubs.o: $(FM_OUT)/fm_import_stubs.S build/config.mk build/fm-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(FM_OUT)/fm.elf: $(FM_OBJS) build/fm-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/fm-module.ld -Map=$(FM_OUT)/fm.map $(FM_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(FM_OUT)/fm_kexports.o
R1_OBJECTS += $(FM_OUT)/fm_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(FM_OUT)/fm_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/fm_module.o $(R1_OBJ)/kernel/fm_module.o: $(FM_GEN)
$(KERNEL_OBJ_PREFIX)kernel/fm_module.o $(R1_OBJ)/kernel/fm_module.o: private CFLAGS_BASE += -I$(FM_OUT)

define pack_fm
	python3 tools/mkmod.py --module-id 6 --elf $(FM_OUT)/fm.elf --metadata $(FM_OUT)/fm_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/fm.mod: $(FM_OUT)/fm.elf $(FM_OUT)/fm_imports.json $(FM_OUT)/fm_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/fm-module.mk
	$(call pack_fm,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/fm.mod: $(FM_OUT)/fm.elf $(FM_OUT)/fm_imports.json $(FM_OUT)/fm_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/fm-module.mk
	$(call pack_fm,$(R1_OUT)/vmkernel.lz4)
-include $(FM_OBJS:.o=.d) $(FM_OUT)/fm_kexports.d $(FM_OUT)/fm_provider_types.d
