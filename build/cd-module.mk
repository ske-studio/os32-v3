# T5b-5: reuse T4 function imports, VA0 packing and paired distribution.
CD_OUT := $(BUILD_OUT)/cd
CD_GEN := $(addprefix $(CD_OUT)/,cd_imports.h cd_imports.json cd_import_slots.c cd_import_stubs.S cd_provider_types.c cd_kexports.c)
CD_DEF_SOURCES := drivers/ata/ide.h drivers/ata/cd_module.h drivers/base/dev.h fs/vfs.h kernel/time/cpu_calibrate.h mm/kmalloc.h lib/kstring.h include/kprintf.h
$(CD_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/cd_imports.def $(CD_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module cd --out-dir $(CD_OUT)

CD_OBJS := $(addprefix $(CD_OUT)/,cd_body.o atapi.o iso9660.o cd_import_slots.o cd_import_stubs.o)
CD_INC := $(INC_KERNEL) -I. -I$(CD_OUT)
$(CD_OUT)/cd_body.o: drivers/ata/cd_body.c $(CD_GEN) build/config.mk build/cd-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_CD_MODULE $(CD_INC) -c $< -o $@
$(CD_OUT)/atapi.o: drivers/ata/atapi.c $(CD_GEN) build/config.mk build/cd-module.mk
	$(CC) $(CFLAGS_BASE) -Os $(CD_INC) -c $< -o $@
$(CD_OUT)/iso9660.o: fs/isofs/iso9660.c $(CD_GEN) build/config.mk build/cd-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_CD_MODULE $(CD_INC) -c $< -o $@
$(CD_OUT)/cd_import_slots.o $(CD_OUT)/cd_provider_types.o $(CD_OUT)/cd_kexports.o: $(CD_OUT)/%.o: $(CD_OUT)/%.c $(CD_GEN) build/config.mk build/cd-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(CD_INC) -c $< -o $@
$(CD_OUT)/cd_import_stubs.o: $(CD_OUT)/cd_import_stubs.S build/config.mk build/cd-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(CD_OUT)/cd.elf: $(CD_OBJS) build/cd-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/cd-module.ld -Map=$(CD_OUT)/cd.map $(CD_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(CD_OUT)/cd_kexports.o
R1_OBJECTS += $(CD_OUT)/cd_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(CD_OUT)/cd_kexports.o
$(KERNEL_OBJ_PREFIX)drivers/ata/cd_module.o $(R1_OBJ)/drivers/ata/cd_module.o: $(CD_GEN)
$(KERNEL_OBJ_PREFIX)drivers/ata/cd_module.o $(R1_OBJ)/drivers/ata/cd_module.o: private CFLAGS_BASE += -I$(CD_OUT)

define pack_cd
	python3 tools/mkmod.py --module-id 4 --elf $(CD_OUT)/cd.elf --metadata $(CD_OUT)/cd_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/cd.mod: $(CD_OUT)/cd.elf $(CD_OUT)/cd_imports.json $(CD_OUT)/cd_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/cd-module.mk
	$(call pack_cd,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/cd.mod: $(CD_OUT)/cd.elf $(CD_OUT)/cd_imports.json $(CD_OUT)/cd_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/cd-module.mk
	$(call pack_cd,$(R1_OUT)/vmkernel.lz4)
-include $(CD_OBJS:.o=.d) $(CD_OUT)/cd_kexports.d $(CD_OUT)/cd_provider_types.d
