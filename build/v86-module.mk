# T5c-3: reuse T4 function imports, VA0 packing and paired distribution.
V86_OUT := $(BUILD_OUT)/v86
V86_GEN := $(addprefix $(V86_OUT)/,v86_imports.h v86_imports.json v86_import_slots.c v86_import_stubs.S v86_provider_types.c v86_kexports.c)
V86_DEF_SOURCES := kernel/v86/v86_module.h kernel/v86/v86_bios.h kernel/v86/v86_io.h kernel/v86/v86_pic.h kernel/v86/v86_kbd.h kernel/module/module.h
$(V86_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/v86_imports.def $(V86_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module v86 --out-dir $(V86_OUT)

V86_OBJS := $(addprefix $(V86_OUT)/,v86_body.o v86_io.o v86_pic.o v86_kbd.o v86_bios.o v86_import_slots.o v86_import_stubs.o)
V86_INC := $(INC_KERNEL) -I. -I$(V86_OUT)
$(V86_OUT)/%.o: kernel/v86/%.c $(V86_GEN) build/config.mk build/v86-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_V86_MODULE $(V86_INC) -c $< -o $@
$(V86_OUT)/v86_import_slots.o $(V86_OUT)/v86_provider_types.o $(V86_OUT)/v86_kexports.o: $(V86_OUT)/%.o: $(V86_OUT)/%.c $(V86_GEN) build/config.mk build/v86-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(V86_INC) -c $< -o $@
$(V86_OUT)/v86_import_stubs.o: $(V86_OUT)/v86_import_stubs.S build/config.mk build/v86-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(V86_OUT)/v86.elf: $(V86_OBJS) build/v86-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/v86-module.ld -Map=$(V86_OUT)/v86.map $(V86_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(V86_OUT)/v86_kexports.o
R1_OBJECTS += $(V86_OUT)/v86_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(V86_OUT)/v86_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/v86/v86_module.o $(R1_OBJ)/kernel/v86/v86_module.o: $(V86_GEN)
$(KERNEL_OBJ_PREFIX)kernel/v86/v86_module.o $(R1_OBJ)/kernel/v86/v86_module.o: private CFLAGS_BASE += -I$(V86_OUT)

define pack_v86
	python3 tools/mkmod.py --module-id 8 --elf $(V86_OUT)/v86.elf --metadata $(V86_OUT)/v86_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/v86.mod: $(V86_OUT)/v86.elf $(V86_OUT)/v86_imports.json $(V86_OUT)/v86_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/v86-module.mk
	$(call pack_v86,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/v86.mod: $(V86_OUT)/v86.elf $(V86_OUT)/v86_imports.json $(V86_OUT)/v86_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/v86-module.mk
	$(call pack_v86,$(R1_OUT)/vmkernel.lz4)
-include $(V86_OBJS:.o=.d) $(V86_OUT)/v86_kexports.d $(V86_OUT)/v86_provider_types.d
