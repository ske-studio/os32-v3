# T5c-5b: reuse T4 function imports, VA0 packing and paired distribution.
GEFORCE_OUT := $(BUILD_OUT)/geforce
GEFORCE_GEN := $(addprefix $(GEFORCE_OUT)/,geforce_imports.h geforce_imports.json geforce_import_slots.c geforce_import_stubs.S geforce_provider_types.c geforce_kexports.c)
GEFORCE_DEF_SOURCES := kernel/geforce_module.h include/gfx_hal.h include/gfx_mode.h include/video_service.h
$(GEFORCE_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/geforce_imports.def $(GEFORCE_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module geforce --out-dir $(GEFORCE_OUT)

GEFORCE_OBJS := $(addprefix $(GEFORCE_OUT)/,geforce_body.o backend_geforce.o gf_present.o geforce_import_slots.o geforce_import_stubs.o)
GEFORCE_INC := $(INC_KERNEL) -I. -I$(GEFORCE_OUT)
$(GEFORCE_OUT)/geforce_body.o: kernel/geforce_body.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/backend_geforce.o: gfx/backend_geforce.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/gf_present.o: gfx/gf_present.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
# P3 service and P2 runner are linked independently of diagnostic HWDEV/KDEBUG.
GEFORCE_SOURCES := $(wildcard hwdev/gf/*.c hwdev/gf/display/*.c hwdev/gf/op/*.c) hwdev/gf_load_state.c kdebug/gfinit_decode.c kdebug/gfinit_pll.c
GEFORCE_SERVICE_OBJS := $(patsubst %.c,$(GEFORCE_OUT)/%.o,$(GEFORCE_SOURCES))
$(GEFORCE_SERVICE_OBJS): $(GEFORCE_OUT)/%.o: %.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION_DECODE $(GEFORCE_INC) -c $< -o $@
GEFORCE_OBJS += $(GEFORCE_SERVICE_OBJS)
$(GEFORCE_OUT)/geforce_import_slots.o $(GEFORCE_OUT)/geforce_provider_types.o $(GEFORCE_OUT)/geforce_kexports.o: $(GEFORCE_OUT)/%.o: $(GEFORCE_OUT)/%.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/geforce_import_stubs.o: $(GEFORCE_OUT)/geforce_import_stubs.S build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(GEFORCE_OUT)/geforce.elf: $(GEFORCE_OBJS) build/geforce-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/geforce-module.ld -Map=$(GEFORCE_OUT)/geforce.map $(GEFORCE_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(GEFORCE_OUT)/geforce_kexports.o
R1_OBJECTS += $(GEFORCE_OUT)/geforce_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(GEFORCE_OUT)/geforce_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/geforce_module.o $(R1_OBJ)/kernel/geforce_module.o: $(GEFORCE_GEN)
$(KERNEL_OBJ_PREFIX)kernel/geforce_module.o $(R1_OBJ)/kernel/geforce_module.o: private CFLAGS_BASE += -I$(GEFORCE_OUT)

define pack_geforce
	python3 tools/mkmod.py --module-id 12 --elf $(GEFORCE_OUT)/geforce.elf --metadata $(GEFORCE_OUT)/geforce_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(R1_OUT)/vmkernel.lz4)
-include $(GEFORCE_OBJS:.o=.d) $(GEFORCE_OUT)/geforce_kexports.d $(GEFORCE_OUT)/geforce_provider_types.d
