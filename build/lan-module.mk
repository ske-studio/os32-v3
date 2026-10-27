# T5c-4: reuse T4 function imports, VA0 packing and paired distribution.
LAN_OUT := $(BUILD_OUT)/lan
LAN_GEN := $(addprefix $(LAN_OUT)/,lan_imports.h lan_imports.json lan_import_slots.c lan_import_stubs.S lan_provider_types.c lan_kexports.c)
LAN_DEF_SOURCES := include/pci_dev_contract.h include/pci_bind_contract.h include/lan_contract.h kernel/lan_module.h drivers/e82557.h kernel/irq.h kernel/dma_pool.h fs/sfs_lan.h fs/sfs_proto.h fs/sfs_client.h kernel/cpu_calibrate.h drivers/pci.h drivers/pci_decode.h drivers/pci_bind.h kernel/sysconfig.h fs/vfs.h include/bootinfo.h lib/kstring.h include/kprintf.h
$(LAN_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/lan_imports.def $(LAN_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module lan --out-dir $(LAN_OUT)

LAN_OBJS := $(addprefix $(LAN_OUT)/,lan_body.o e82557.o e82557_eeprom.o e82557_desc.o sfs_lan.o lan_import_slots.o lan_import_stubs.o)
LAN_INC := $(INC_KERNEL) -I. -I$(LAN_OUT)
$(LAN_OUT)/lan_body.o: kernel/lan_body.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/e82557.o $(LAN_OUT)/e82557_eeprom.o $(LAN_OUT)/e82557_desc.o: $(LAN_OUT)/%.o: drivers/%.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/sfs_lan.o: fs/sfs_lan.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/lan_import_slots.o $(LAN_OUT)/lan_provider_types.o $(LAN_OUT)/lan_kexports.o: $(LAN_OUT)/%.o: $(LAN_OUT)/%.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(LAN_INC) -c $< -o $@
$(LAN_OUT)/lan_import_stubs.o: $(LAN_OUT)/lan_import_stubs.S build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(LAN_OUT)/lan.elf: $(LAN_OBJS) build/lan-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/lan-module.ld -Map=$(LAN_OUT)/lan.map $(LAN_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(LAN_OUT)/lan_kexports.o
R1_OBJECTS += $(LAN_OUT)/lan_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(LAN_OUT)/lan_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/lan_module.o $(R1_OBJ)/kernel/lan_module.o: $(LAN_GEN)
$(KERNEL_OBJ_PREFIX)kernel/lan_module.o $(R1_OBJ)/kernel/lan_module.o: private CFLAGS_BASE += -I$(LAN_OUT)

define pack_lan
	python3 tools/mkmod.py --module-id 9 --elf $(LAN_OUT)/lan.elf --metadata $(LAN_OUT)/lan_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/lan.mod: $(LAN_OUT)/lan.elf $(LAN_OUT)/lan_imports.json $(LAN_OUT)/lan_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/lan-module.mk
	$(call pack_lan,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/lan.mod: $(LAN_OUT)/lan.elf $(LAN_OUT)/lan_imports.json $(LAN_OUT)/lan_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/lan-module.mk
	$(call pack_lan,$(R1_OUT)/vmkernel.lz4)
-include $(LAN_OBJS:.o=.d) $(LAN_OUT)/lan_kexports.d $(LAN_OUT)/lan_provider_types.d
