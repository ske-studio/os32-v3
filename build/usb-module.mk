# USB body and typed imports, paired separately with each PC-98 kernel.
USB_OUT := $(BUILD_OUT)/usb
USB_GEN := $(addprefix $(USB_OUT)/,usb_imports.h usb_imports.json usb_import_slots.c usb_import_stubs.S usb_provider_types.c usb_kexports.c)
USB_DEF_SOURCES := input/hal/usb_input.h mm/dma_pool.h input/hal/in2_int.h mm/kmalloc.h lib/kstring.h include/kprintf.h kernel/module/module_runtime.h platform/pc98/usb_resident.h include/usb_platform.h drivers/usb/usb_core.h
$(USB_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py tools/prototypes/usb_imports.def $(USB_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module usb --out-dir $(USB_OUT)

USB_OBJS := $(USB_OUT)/usb_module.o $(USB_OUT)/usb_import_slots.o $(USB_OUT)/usb_import_stubs.o
USB_INC := $(INC_KERNEL) -I. -I$(USB_OUT)
$(USB_OUT)/usb_import_slots.o $(USB_OUT)/usb_provider_types.o $(USB_OUT)/usb_kexports.o: $(USB_OUT)/%.o: $(USB_OUT)/%.c $(USB_GEN) build/config.mk build/usb-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(USB_INC) -c $< -o $@
$(USB_OUT)/usb_import_stubs.o: $(USB_OUT)/usb_import_stubs.S build/config.mk build/usb-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(USB_OUT)/usb.elf: $(USB_OBJS) build/usb-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/usb-module.ld -Map=$(USB_OUT)/usb.map $(USB_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(USB_OUT)/usb_kexports.o
R1_OBJECTS += $(USB_OUT)/usb_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(USB_OUT)/usb_kexports.o
$(KERNEL_OBJ_PREFIX)platform/pc98/usb_load.o $(R1_OBJ)/platform/pc98/usb_load.o: $(USB_GEN)
$(KERNEL_OBJ_PREFIX)platform/pc98/usb_load.o $(R1_OBJ)/platform/pc98/usb_load.o: private CFLAGS_BASE += -I$(USB_OUT)

define pack_usb
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 14 --elf $(USB_OUT)/usb.elf --metadata $(USB_OUT)/usb_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/usb.mod: $(USB_OUT)/usb.elf $(USB_OUT)/usb_imports.json $(USB_OUT)/usb_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/usb-module.mk
	$(call pack_usb,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/usb.mod: $(USB_OUT)/usb.elf $(USB_OUT)/usb_imports.json $(USB_OUT)/usb_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/usb-module.mk
	$(call pack_usb,$(R1_OUT)/vmkernel.lz4)
-include $(USB_OBJS:.o=.d) $(USB_OUT)/usb_kexports.d $(USB_OUT)/usb_provider_types.d

