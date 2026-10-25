# The module and kernel share the actual machine/ABI/optimization flags.
ifeq ($(origin KERNEL_CFLAGS),undefined)
include build/config.mk
endif
KDEBUG_OUT ?= $(BUILD_OUT)/kdebug
KDEBUG_STAGE := S6
KDEBUG_GEN := kdebug/generated
MODULE_SCHEMA := kernel/module_schema.h
$(MODULE_SCHEMA): tools/module_schema.py tools/gen_module_schema.py
	python3 tools/gen_module_schema.py

KDEBUG_OBJS := $(KDEBUG_OUT)/v86_gcap.o $(KDEBUG_OUT)/v86_gcap_math.o $(KDEBUG_OUT)/nic_trace.o $(KDEBUG_OUT)/kselftest.o $(KDEBUG_OUT)/selftest_con_sink.o $(KDEBUG_OUT)/selftest_kbd_inject.o $(KDEBUG_OUT)/selftest_lease.o $(KDEBUG_OUT)/crt_diag.o $(KDEBUG_OUT)/kdebug_main.o \
               $(KDEBUG_OUT)/kpeek.o $(KDEBUG_OUT)/kdebug_window.o $(KDEBUG_OUT)/kpeek_allow.o $(KDEBUG_OUT)/recover.o $(KDEBUG_OUT)/recover_profile.o $(KDEBUG_OUT)/recover_virtual.o \
               $(KDEBUG_OUT)/kdebug_import_slots.o $(KDEBUG_OUT)/kdebug_import_stubs.o
KDEBUG_KEXPORT_OBJ := $(KDEBUG_OUT)/kdebug_kexports.o
C_KERNEL_OBJ += $(KDEBUG_KEXPORT_OBJ)
# kernel.elf's prerequisites were expanded before this include.
$(BUILD_OUT)/kernel.elf: $(KDEBUG_KEXPORT_OBJ)

# Generate all contracts from the same .def and source declarations.
KDEBUG_DEF_SOURCES := $(shell python3 -c 'import json; print(" ".join(sorted({e["source"] for e in json.load(open("kernel/kdebug_imports.def"))["imports"] if "source" in e})))')
# Keep generated source inputs in the tree, like the KAPI contract: read-only
# static checks (including CI without a cross compiler) need these under -n.
KDEBUG_GENERATED := $(addprefix $(KDEBUG_GEN)/,kdebug_imports.h kdebug_imports.json \
    kdebug_kexports.c kdebug_import_slots.c kdebug_import_stubs.S kdebug_import_macros.h)
$(KDEBUG_GENERATED) &: kernel/kdebug_imports.def tools/gen_kdebug_imports.py tools/mkmod.py tools/module_schema.py build/kdebug.mk $(MODULE_SCHEMA) $(KDEBUG_DEF_SOURCES)
	python3 tools/gen_kdebug_imports.py --stage $(KDEBUG_STAGE) --out-dir $(KDEBUG_GEN)

kernel/module.o: kernel/module.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_imports.h $(MODULE_SCHEMA)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(KDEBUG_GEN) -c kernel/module.c -o $@

$(KDEBUG_OUT)/recover_profile.o: KERNEL_CFLAGS += -DRECOVER_MODULE

$(KDEBUG_OUT)/%.o: kdebug/%.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_import_macros.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/kdebug_kexports.o $(KDEBUG_OUT)/kdebug_import_slots.o: $(KDEBUG_OUT)/%.o: $(KDEBUG_GEN)/%.c $(KDEBUG_GEN)/kdebug_imports.h build/config.mk build/kdebug.mk
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(KDEBUG_GEN) -c $< -o $@

$(KDEBUG_OUT)/kdebug_import_stubs.o: $(KDEBUG_GEN)/kdebug_import_stubs.S build/config.mk build/kdebug.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@

$(KDEBUG_OUT)/kdebug.elf: $(KDEBUG_OBJS) build/module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -Map=$(KDEBUG_OUT)/kdebug.map -T build/module.ld $(KDEBUG_OBJS) $(KDEBUG_OUT)/hwdev_core.o -o $@

# Values come from their authoritative definitions, including before S1b's bump.
KDEBUG_KAPI_GEN = $(shell python3 -c 'import json; print(json.load(open("sdk/kapi.json"))["version"])')
KDEBUG_LAYOUT_GEN = $(shell sed -n 's/^\#define OS32_MEMORY_LAYOUT_GENERATION \([0-9]*\).*/\1/p' sdk/include/os32/os32_generations.h)

define pack_kdebug
	python3 tools/mkmod.py --elf $(KDEBUG_OUT)/kdebug.elf --metadata $(KDEBUG_GEN)/kdebug_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef

$(BUILD_OUT)/kdebug.bin: $(KDEBUG_OUT)/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json sdk/include/os32/os32_generations.h build/kdebug.mk
	$(call pack_kdebug,$(BUILD_OUT)/vmkernel.lz4)

$(R1_OUT)/kdebug.bin: $(KDEBUG_OUT)/kdebug.elf $(KDEBUG_GEN)/kdebug_imports.json $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json sdk/include/os32/os32_generations.h build/kdebug.mk
	$(call pack_kdebug,$(R1_OUT)/vmkernel.lz4)

$(R1_OBJ)/kernel/module.o: kernel/module.c build/config.mk build/kdebug.mk $(KDEBUG_GEN)/kdebug_imports.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) -DOS32_R1_FIXTURE $(INC_KERNEL) -I$(KDEBUG_GEN) -c kernel/module.c -o $@
$(R1_OUT)/kernel.elf: $(KDEBUG_KEXPORT_OBJ)
R1_OBJECTS += $(KDEBUG_KEXPORT_OBJ)

all: $(BUILD_OUT)/kdebug.bin
kernel/module_image.o kernel/module_runtime.o $(R1_OBJ)/kernel/module.o \
    $(R1_OBJ)/kernel/module_image.o $(R1_OBJ)/kernel/module_runtime.o \
    $(KERNEL_OBJ_PREFIX)kernel/module.o $(KERNEL_OBJ_PREFIX)kernel/module_image.o \
    $(KERNEL_OBJ_PREFIX)kernel/module_runtime.o: $(MODULE_SCHEMA)
-include $(KDEBUG_OBJS:.o=.d) $(KDEBUG_KEXPORT_OBJ:.o=.d)

# One shared contract, compiled with the kernel machine/ABI flags.
HWDEV_GEN := hwdev/generated
HWDEV_HEADERS := $(wildcard include/*.h kernel/*.h drivers/*.h lib/*.h fs/*.h exec/*.h gfx/*.h arch/x86/*.h platform/pc98/*.h sdk/include/os32/*.h kdebug/*.h)
HWDEV_GENERATED := $(addprefix $(HWDEV_GEN)/,hwdev_contract.h hwdev_core.c hwdev_imports.h hwdev_layout.txt hwdev_import_macros.h hwdev_import_slots.c hwdev_import_stubs.S hwdev_imports.json)
$(HWDEV_GENERATED) &: hwdev/hwdev_imports.def kernel/kdebug_imports.def tools/gen_kdebug_imports.py build/config.mk build/kdebug.mk tools/gen_hwdev_contract.py tools/hwdev_layout.json docs/decisions/realhw/HWDEV_CORE_API.md $(HWDEV_HEADERS)
	python3 tools/gen_hwdev_contract.py --cc $(CROSS_DIR)/bin/i386-elf-gcc --flags '$(filter-out -MMD -MP,$(KERNEL_CFLAGS))' --includes '$(INC_KERNEL)'
$(KDEBUG_OUT)/hwdev_core.o: $(HWDEV_GEN)/hwdev_core.c $(HWDEV_GEN)/hwdev_contract.h
	mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) $(INC_KERNEL) -I$(HWDEV_GEN) -c $< -o $@
$(KDEBUG_OUT)/kdebug_main.o kernel/hwdev_module.o $(KERNEL_OBJ_PREFIX)kernel/hwdev_module.o $(R1_OBJ)/kernel/hwdev_module.o: $(HWDEV_GENERATED)
$(KDEBUG_OUT)/kdebug.elf: $(KDEBUG_OUT)/hwdev_core.o

-include $(KDEBUG_OUT)/hwdev_core.d
