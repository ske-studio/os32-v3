# T5a-2: reuse T4 function imports, VA0 packing and paired distribution.
FEP_OUT := $(BUILD_OUT)/fep
FEP_GEN := $(addprefix $(FEP_OUT)/,fep_imports.h fep_imports.json fep_import_slots.c fep_import_stubs.S fep_provider_types.c fep_kexports.c)
FEP_DEF_SOURCES := include/redir_access.h fs/fd_redirect.h include/con_sink.h drivers/input/kbd.h mm/kmalloc.h lib/kstring.h include/kprintf.h lib/sqlite3/os32_sqlite_outer.h exec/exec.h drivers/char/serial.h kernel/sqlite_engine.h include/tvram.h lib/utf8.h fs/vfs.h kernel/ime/fep_module.h kernel/ime/ime_render.h sdk/include/os32/os32_kapi_shared.h
$(FEP_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/fep_imports.def $(FEP_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module fep --out-dir $(FEP_OUT)

FEP_OBJS := $(addprefix $(FEP_OUT)/,fep_body.o ime.o ime_dict.o ime_romkana.o ime_render_tvram.o fep_import_slots.o fep_import_stubs.o)
FEP_INC := $(INC_KERNEL) -I. -I$(FEP_OUT)
$(FEP_OUT)/fep_body.o $(FEP_OUT)/ime.o $(FEP_OUT)/ime_dict.o $(FEP_OUT)/ime_romkana.o $(FEP_OUT)/ime_render_tvram.o: $(FEP_OUT)/%.o: kernel/ime/%.c $(FEP_GEN) build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FEP_MODULE $(FEP_INC) -c $< -o $@
$(FEP_OUT)/fep_import_slots.o $(FEP_OUT)/fep_provider_types.o $(FEP_OUT)/fep_kexports.o: $(FEP_OUT)/%.o: $(FEP_OUT)/%.c $(FEP_GEN) build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(FEP_INC) -c $< -o $@
$(FEP_OUT)/fep_import_stubs.o: $(FEP_OUT)/fep_import_stubs.S build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(FEP_OUT)/fep.elf: $(FEP_OBJS) build/fep-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/fep-module.ld -Map=$(FEP_OUT)/fep.map $(FEP_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(FEP_OUT)/fep_kexports.o
R1_OBJECTS += $(FEP_OUT)/fep_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(FEP_OUT)/fep_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/ime/fep_module.o $(R1_OBJ)/kernel/ime/fep_module.o: $(FEP_GEN)
$(KERNEL_OBJ_PREFIX)kernel/ime/fep_module.o $(R1_OBJ)/kernel/ime/fep_module.o: private CFLAGS_BASE += -I$(FEP_OUT)

define pack_fep
	python3 tools/mkmod.py --module-id 3 --elf $(FEP_OUT)/fep.elf --metadata $(FEP_OUT)/fep_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/fep.mod: $(FEP_OUT)/fep.elf $(FEP_OUT)/fep_imports.json $(FEP_OUT)/fep_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/fep-module.mk
	$(call pack_fep,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/fep.mod: $(FEP_OUT)/fep.elf $(FEP_OUT)/fep_imports.json $(FEP_OUT)/fep_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/fep-module.mk
	$(call pack_fep,$(R1_OUT)/vmkernel.lz4)
-include $(FEP_OBJS:.o=.d) $(FEP_OUT)/fep_kexports.d $(FEP_OUT)/fep_provider_types.d
