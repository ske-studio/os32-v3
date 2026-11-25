# T5c-4: reuse T4 function imports, VA0 packing and paired distribution.
LAN_OUT := $(BUILD_OUT)/lan
LAN_GEN := $(addprefix $(LAN_OUT)/,lan_imports.h lan_imports.json lan_import_slots.c lan_import_stubs.S lan_provider_types.c lan_kexports.c)
LAN_DEF_SOURCES := sys/net/nic.h sys/include/pci_dev_contract.h sys/include/pci_contract.h sys/include/pci_bind_contract.h sys/include/lan_contract.h sys/dev/net/e82557/lan_module.h sys/dev/net/e82557/e82557.h sys/kern/irq/irq.h sys/kern/mm/dma_pool.h sys/fs/serialfs/sfs_lan.h sys/fs/serialfs/sfs_proto.h sys/fs/serialfs/sfs_client.h sys/fs/serialfs/serialfs.h sys/hal/block/dev.h sys/net/host_mailbox.h sdk/include/os32/os32_host_notice.h sys/hal/time/cpu_calibrate.h sys/dev/pci/pci.h sys/dev/pci/pci_decode.h sys/dev/pci/pci_bind.h sys/kern/init/sysconfig.h sys/fs/vfs.h sys/include/bootinfo.h sys/kern/kstring.h sys/include/kprintf.h
$(LAN_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/lan_imports.def $(LAN_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module lan --out-dir $(LAN_OUT)

# Later stages supply these bodies. Register their rules now; never build a
# second resident copy. Once present, each source joins lan.mod automatically.
LAN_HOSTSVC_SOURCES := $(wildcard sys/dev/net/e82557/nic_e82557.c) $(wildcard sys/fs/serialfs/sfs_block.c) $(wildcard sys/dev/block/netcd.c)
LAN_HOSTSVC_OBJS := $(addprefix $(LAN_OUT)/,$(notdir $(LAN_HOSTSVC_SOURCES:.c=.o)))
LAN_OBJS := $(addprefix $(LAN_OUT)/,lan_body.o e82557.o e82557_eeprom.o e82557_desc.o sfs_lan.o lan_import_slots.o lan_import_stubs.o) $(LAN_HOSTSVC_OBJS)
$(LAN_OUT)/nic_e82557.o: sys/dev/net/e82557/nic_e82557.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/sfs_block.o: sys/fs/serialfs/sfs_block.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/netcd.o: sys/dev/block/netcd.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
LAN_INC := $(INC_KERNEL) -I. -I$(LAN_OUT)
$(LAN_OUT)/lan_body.o: sys/dev/net/e82557/lan_body.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/e82557.o $(LAN_OUT)/e82557_eeprom.o $(LAN_OUT)/e82557_desc.o: $(LAN_OUT)/%.o: sys/dev/net/e82557/%.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/sfs_lan.o: sys/fs/serialfs/sfs_lan.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_LAN_MODULE $(LAN_INC) -c $< -o $@
$(LAN_OUT)/lan_import_slots.o $(LAN_OUT)/lan_provider_types.o $(LAN_OUT)/lan_kexports.o: $(LAN_OUT)/%.o: $(LAN_OUT)/%.c $(LAN_GEN) build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(LAN_INC) -c $< -o $@
$(LAN_OUT)/lan_import_stubs.o: $(LAN_OUT)/lan_import_stubs.S build/config.mk build/lan-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(LAN_OUT)/lan.elf: $(LAN_OBJS) build/lan-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/lan-module.ld -Map=$(LAN_OUT)/lan.map $(LAN_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(LAN_OUT)/lan_kexports.o
R1_OBJECTS += $(LAN_OUT)/lan_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(LAN_OUT)/lan_kexports.o
$(KERNEL_OBJ_PREFIX)sys/dev/net/e82557/lan_module.o $(R1_OBJ)/sys/dev/net/e82557/lan_module.o: $(LAN_GEN)
$(KERNEL_OBJ_PREFIX)sys/dev/net/e82557/lan_module.o $(R1_OBJ)/sys/dev/net/e82557/lan_module.o: private CFLAGS_BASE += -I$(LAN_OUT)

define pack_lan
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 9 --elf $(LAN_OUT)/lan.elf --metadata $(LAN_OUT)/lan_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/lan.mod: $(LAN_OUT)/lan.elf $(LAN_OUT)/lan_imports.json $(LAN_OUT)/lan_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/lan-module.mk
	$(call pack_lan,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/lan.mod: $(LAN_OUT)/lan.elf $(LAN_OUT)/lan_imports.json $(LAN_OUT)/lan_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/lan-module.mk
	$(call pack_lan,$(R1_OUT)/vmkernel.lz4)
-include $(LAN_OBJS:.o=.d) $(LAN_OUT)/lan_kexports.d $(LAN_OUT)/lan_provider_types.d
