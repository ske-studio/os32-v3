# Trident S3: VA0 body, function imports, paired optional GUI distribution.
TRIDENT_OUT := $(BUILD_OUT)/trident
TRIDENT_GEN := $(addprefix $(TRIDENT_OUT)/,trident_imports.h trident_imports.json trident_import_slots.c trident_import_stubs.S trident_provider_types.c trident_kexports.c)
TRIDENT_DEF_SOURCES := drivers/video/trident/trident_module.h include/gfx_hal.h include/gfx_mode.h drivers/video/trident/trident.h gfx/palette.h lib/serial_diag.h mm/kmalloc.h drivers/char/serial.h
$(TRIDENT_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py tools/prototypes/trident_imports.def $(TRIDENT_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module trident --out-dir $(TRIDENT_OUT)

TRIDENT_PARTS := trident_body backend_trident probe regs default_dac state_save state_restore pc98_init vram mode_640 route_ra266 present palette lifecycle
TRIDENT_OBJS := $(addprefix $(TRIDENT_OUT)/,$(addsuffix .o,$(TRIDENT_PARTS)) trident_import_slots.o trident_import_stubs.o)
TRIDENT_INC := $(INC_KERNEL) -I. -I$(TRIDENT_OUT)
$(addprefix $(TRIDENT_OUT)/,$(addsuffix .o,$(TRIDENT_PARTS))): $(TRIDENT_OUT)/%.o: drivers/video/trident/%.c $(TRIDENT_GEN) build/config.mk build/trident-module.mk
	$(CC) $(CFLAGS_BASE) -Os $(TRIDENT_INC) -c $< -o $@
$(TRIDENT_OUT)/trident_import_slots.o $(TRIDENT_OUT)/trident_provider_types.o $(TRIDENT_OUT)/trident_kexports.o: $(TRIDENT_OUT)/%.o: $(TRIDENT_OUT)/%.c $(TRIDENT_GEN) build/config.mk build/trident-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(TRIDENT_INC) -c $< -o $@
$(TRIDENT_OUT)/trident_import_stubs.o: $(TRIDENT_OUT)/trident_import_stubs.S build/config.mk build/trident-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(TRIDENT_OUT)/trident.elf: $(TRIDENT_OBJS) build/trident-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/trident-module.ld -Map=$(TRIDENT_OUT)/trident.map $(TRIDENT_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(TRIDENT_OUT)/trident_kexports.o
R1_OBJECTS += $(TRIDENT_OUT)/trident_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(TRIDENT_OUT)/trident_kexports.o
$(KERNEL_OBJ_PREFIX)drivers/video/trident/trident_module.o $(R1_OBJ)/drivers/video/trident/trident_module.o: $(TRIDENT_GEN)
$(KERNEL_OBJ_PREFIX)drivers/video/trident/trident_module.o $(R1_OBJ)/drivers/video/trident/trident_module.o: private CFLAGS_BASE += -I$(TRIDENT_OUT)

define pack_trident
	python3 tools/mkmod.py --module-id 13 --elf $(TRIDENT_OUT)/trident.elf --metadata $(TRIDENT_OUT)/trident_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/trident.mod: $(TRIDENT_OUT)/trident.elf $(TRIDENT_OUT)/trident_imports.json $(TRIDENT_OUT)/trident_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/trident-module.mk
	$(call pack_trident,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/trident.mod: $(TRIDENT_OUT)/trident.elf $(TRIDENT_OUT)/trident_imports.json $(TRIDENT_OUT)/trident_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/trident-module.mk
	$(call pack_trident,$(R1_OUT)/vmkernel.lz4)
-include $(TRIDENT_OBJS:.o=.d) $(TRIDENT_OUT)/trident_kexports.d $(TRIDENT_OUT)/trident_provider_types.d
