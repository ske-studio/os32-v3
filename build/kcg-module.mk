# T5c-1: reuse T4 function imports, VA0 packing and paired distribution.
KCG_OUT := $(BUILD_OUT)/kcg
KCG_GEN := $(addprefix $(KCG_OUT)/,kcg_imports.h kcg_imports.json kcg_import_slots.c kcg_import_stubs.S kcg_provider_types.c kcg_kexports.c)
KCG_DEF_SOURCES := kernel/kcg_module.h lib/kstring.h include/kprintf.h
$(KCG_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/kcg_imports.def $(KCG_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module kcg --out-dir $(KCG_OUT)

KCG_OBJS := $(addprefix $(KCG_OUT)/,kcg_body.o kcg_import_slots.o kcg_import_stubs.o)
KCG_INC := $(INC_KERNEL) -I. -I$(KCG_OUT)
$(KCG_OUT)/kcg_body.o: kernel/kcg_body.c $(KCG_GEN) build/config.mk build/kcg-module.mk
	$(CC) $(CFLAGS_BASE) -Os $(KCG_INC) -c $< -o $@
$(KCG_OUT)/kcg_import_slots.o $(KCG_OUT)/kcg_provider_types.o $(KCG_OUT)/kcg_kexports.o: $(KCG_OUT)/%.o: $(KCG_OUT)/%.c $(KCG_GEN) build/config.mk build/kcg-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(KCG_INC) -c $< -o $@
$(KCG_OUT)/kcg_import_stubs.o: $(KCG_OUT)/kcg_import_stubs.S build/config.mk build/kcg-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(KCG_OUT)/kcg.elf: $(KCG_OBJS) build/kcg-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/kcg-module.ld -Map=$(KCG_OUT)/kcg.map $(KCG_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(KCG_OUT)/kcg_kexports.o
R1_OBJECTS += $(KCG_OUT)/kcg_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(KCG_OUT)/kcg_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/kcg_module.o $(R1_OBJ)/kernel/kcg_module.o: $(KCG_GEN)
$(KERNEL_OBJ_PREFIX)kernel/kcg_module.o $(R1_OBJ)/kernel/kcg_module.o: private CFLAGS_BASE += -I$(KCG_OUT)

define pack_kcg
	python3 tools/mkmod.py --module-id 5 --elf $(KCG_OUT)/kcg.elf --metadata $(KCG_OUT)/kcg_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/kcg.mod: $(KCG_OUT)/kcg.elf $(KCG_OUT)/kcg_imports.json $(KCG_OUT)/kcg_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/kcg-module.mk
	$(call pack_kcg,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/kcg.mod: $(KCG_OUT)/kcg.elf $(KCG_OUT)/kcg_imports.json $(KCG_OUT)/kcg_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/kcg-module.mk
	$(call pack_kcg,$(R1_OUT)/vmkernel.lz4)
-include $(KCG_OBJS:.o=.d) $(KCG_OUT)/kcg_kexports.d $(KCG_OUT)/kcg_provider_types.d
