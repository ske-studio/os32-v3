# Shared kernel object derivation and path-specific compiler flags.
C_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(C_KERNEL:.c=.o))
ASM_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o) $(ASM_EXTRA_OBJ))
GAS_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(GAS_KERNEL:.S=.o))

KERNEL_CFLAGS += $(DIAGNOSTIC_FLAG_SPLASH_DIAG_BUILD) $(DIAGNOSTIC_FLAG_SPLASH_OBS_BUILD) $(DIAGNOSTIC_FLAG_PCM_E4_DIAG_BUILD)
.FORCE:

ifeq ($(ARCH),x86)
$(KERNEL_OBJ_PREFIX)arch/x86/kernel/mtrr_wc.o: arch/x86/kernel/mtrr_wc.h

# Test-only real guard injection: 0=off, 1=kernel, 2=SQLite engine.
DF_GUARD_TEST ?= 0
$(KERNEL_OBJ_PREFIX)arch/x86/kernel/double_fault.o: private KERNEL_CFLAGS += -Os -DOS32_DF_GUARD_TEST=$(DF_GUARD_TEST)
$(KERNEL_OBJ_PREFIX)arch/x86/kernel/double_fault.o: $(BUILD_OUT)/config/df-guard.flags

# Detect command-line options without making unchanged objects newer than VK32.
# Like build_id.c and memmap_layout.h, these files preserve mtime on equality.
$(BUILD_OUT)/config/df-guard.flags: private BUILD_OPTION = $(CC) $(CFLAGS_BASE) DF_GUARD_TEST=$(DF_GUARD_TEST)
$(BUILD_OUT)/config/lgy98.flags: private BUILD_OPTION = $(CC) $(CFLAGS_BASE)
$(BUILD_OUT)/config/source-date.flags: private BUILD_OPTION = $(CC) $(CFLAGS_BASE) SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)
$(BUILD_OUT)/config/df-guard.flags $(BUILD_OUT)/config/lgy98.flags $(BUILD_OUT)/config/source-date.flags: .FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(BUILD_OPTION)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

# Resident debug loader and validator: preserve the source-adjacent flags.
$(addprefix $(KERNEL_OBJ_PREFIX),kernel/module/module.o kernel/module/module_image.o kernel/module/module_runtime.o): private KERNEL_CFLAGS += -Os

# 常駐 shell の内部入口 (T7b-b2、exec/shell_service.c)。wrapper は
# __builtin_return_address(0) で呼び手を確かめるので、このファイルだけ
# 尾呼出しを封じて組む (設計 §3-2)。
SHELL_SERVICE_OBJ = $(KERNEL_OBJ_PREFIX)exec/shell_service.o
$(SHELL_SERVICE_OBJ): private KERNEL_CFLAGS += -fno-optimize-sibling-calls
# S3b から C_KERNEL に入る (結線は exec/exec.c)。旗が付くことは
# `make -n` で確かめる (tools/tests/test_shell_service.py の静的検査も見る)。

# T2h: entirely separate objects, dependencies, build ID, map and image.
R1_OUT := $(BUILD_OUT)/r1
R1_OBJ := $(R1_OUT)/obj
$(R1_OBJ)/exec/shell_service.o: private KERNEL_CFLAGS += -fno-optimize-sibling-calls
R1_C := $(C_KERNEL) kernel/r1_fixture.c kernel/font_guard_fixture.c
R1_OBJECTS := $(addprefix $(R1_OBJ)/,$(ASM_KERNEL:.asm=.o) exec/ring3_ls_shim.o $(R1_C:.c=.o)) $(R1_OBJ)/build_id.o
R1_SMALL := exec/exec_heap.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c mm/paging_app.c

$(R1_OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(R1_SMALL)),-Os) -DOS32_R1_FIXTURE $(if $(filter fs/fat/fatfs/%,$<),$(INC_FATFS),$(INC_KERNEL)) -c $< -o $@

$(R1_OBJ)/%.o: %.asm sdk/crt/generations.inc arch/x86/kernel/cpu_flags.inc arch/x86/kernel/entry_layout.inc arch/x86/kernel/iret_user.inc
	@mkdir -p $(@D)
	$(LAYOUT_CPP) -traditional-cpp $< -o $@.asm
	$(AS) -p sdk/crt/generations.inc -f elf32 $@.asm -o $@

$(R1_OBJ)/exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h include/cpu.h arch/x86/kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

$(R1_OUT)/build_id.c: .FORCE
	@mkdir -p $(@D)
	@python3 tools/gen_build_id.py -o $@

$(R1_OBJ)/build_id.o: $(R1_OUT)/build_id.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -DOS32_R1_FIXTURE -Iinclude -c $< -o $@

$(R1_OBJ)/exec/exec.o $(R1_OBJ)/kapi/kapi_generated.o: kapi/kapi_generated.c
$(R1_OBJ)/drivers/net/ne2000/lgy98.o: $(BUILD_OUT)/config/lgy98.flags
$(R1_OBJ)/kapi/kapi_sys.o: $(BUILD_OUT)/config/source-date.flags

endif

# Core objects always live under BUILD_OUT. Preserve each original -I order;
# the R1 fixture above deliberately keeps its existing INC_KERNEL profile.
LAYOUT_SMALL = $(R1_SMALL)
$(KERNEL_OBJ_PREFIX)%.o: private KERNEL_INCLUDES = $(INC_KERNEL)
$(KERNEL_OBJ_PREFIX)drivers/%.o: private KERNEL_INCLUDES = $(INC_DRIVERS)
$(KERNEL_OBJ_PREFIX)gfx/%.o: private KERNEL_INCLUDES = $(INC_GFX)
$(KERNEL_OBJ_PREFIX)fs/%.o: private KERNEL_INCLUDES = $(INC_FS)
$(KERNEL_OBJ_PREFIX)fs/fat/fatfs/%.o: private KERNEL_INCLUDES = $(INC_FATFS)
$(KERNEL_OBJ_PREFIX)exec/%.o: private KERNEL_INCLUDES = $(INC_EXEC)
$(KERNEL_OBJ_PREFIX)kapi/%.o: private KERNEL_INCLUDES = $(INC_KAPI)
$(KERNEL_OBJ_PREFIX)lib/%.o: private KERNEL_INCLUDES = $(INC_LIB)
$(addprefix $(KERNEL_OBJ_PREFIX),drivers/video/cirrus/backend_cirrus.o drivers/video/cirrus/cirrus_identify.o drivers/video/geforce/backend_geforce.o drivers/video/geforce/gf_present.o drivers/video/pc98/backend_pc98.o drivers/video/pc98/backend_pegc.o drivers/video/pc98/pc98_route.o): private KERNEL_INCLUDES = $(INC_GFX)
CORE_KERNEL_INCLUDES = drivers/video/trident/trident_module.o drivers/video/trident/resources.o drivers/ata/cd_body.o drivers/ata/cd_module.o drivers/net/e82557/lan_body.o drivers/net/e82557/lan_module.o drivers/sound/cs4231/pcm_body.o drivers/sound/cs4231/pcm_module.o drivers/sound/fm/fm_body.o drivers/sound/fm/fm_module.o drivers/sound/fm/snd_engine.o drivers/video/cirrus/cirrus_body.o drivers/video/cirrus/cirrus_module.o drivers/video/geforce/geforce_body.o drivers/video/geforce/geforce_module.o drivers/video/geforce/geforce_platform.o drivers/video/kcg/kcg_body.o drivers/video/kcg/kcg_module.o drivers/input/mouse.o drivers/block/loop_dev.o drivers/net/ne2000/ne2000.o drivers/net/ne2000/lgy98.o drivers/block/fdc.o fs/serialfs/serialfs_session.o lib/sqlite3/os32_sqlite_test.o
$(addprefix $(KERNEL_OBJ_PREFIX),$(CORE_KERNEL_INCLUDES)): private KERNEL_INCLUDES = $(INC_KERNEL)

$(KERNEL_OBJ_PREFIX)%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(LAYOUT_SMALL)),-Os) $(KERNEL_INCLUDES) -c $< -o $@

ifeq ($(ARCH),x86)
$(KERNEL_OBJ_PREFIX)exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h include/cpu.h arch/x86/kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

$(addprefix $(KERNEL_OBJ_PREFIX),exec/ring3_ls.o exec/exec.o): exec/ring3_ls.h
$(addprefix $(KERNEL_OBJ_PREFIX),$(filter exec/%.o kapi/%.o,$(C_KERNEL:.c=.o))): kapi/kapi_generated.c
$(KERNEL_OBJ_PREFIX)drivers/net/ne2000/lgy98.o: $(BUILD_OUT)/config/lgy98.flags
$(KERNEL_OBJ_PREFIX)kapi/kapi_sys.o: $(BUILD_OUT)/config/source-date.flags
$(KERNEL_OBJ_PREFIX)fs/fat/fatfs/ff.o: fs/fat/fatfs/ffconf.h fs/fat/fatfs/string.h
$(KERNEL_OBJ_PREFIX)fs/fat/fatfs/diskio.o: fs/fat/fatfs/ff.h fs/fat/fatfs/diskio.h

$(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o)): $(KERNEL_OBJ_PREFIX)%.o: %.asm sdk/crt/generations.inc arch/x86/kernel/cpu_flags.inc arch/x86/kernel/entry_layout.inc arch/x86/kernel/iret_user.inc
	@mkdir -p $(@D) $(dir $(BUILD_OUT)/asm/$<)
	$(LAYOUT_CPP) -traditional-cpp $< -o $(BUILD_OUT)/asm/$<
	$(AS) -p sdk/crt/generations.inc -f elf32 $(BUILD_OUT)/asm/$< -o $@

# The boot selftest stays in core and calls the same typed facade as DB/FEP.
$(KERNEL_OBJ_PREFIX)lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

endif

ifneq ($(strip $(GAS_KERNEL_OBJ)),)
$(GAS_KERNEL_OBJ): $(KERNEL_OBJ_PREFIX)%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_MACHINE) $(INC_COMMON) -c $< -o $@
endif

$(C_KERNEL_OBJ) $(ASM_KERNEL_OBJ) $(GAS_KERNEL_OBJ): $(PROFILE_MAKEFILES)
.PHONY: .FORCE kernel-compile
kernel-compile: $(C_KERNEL_OBJ) $(ASM_KERNEL_OBJ) $(GAS_KERNEL_OBJ)

# Refuse a cross-target reuse of an object directory before any recipe runs.
TARGET_STAMP = $(BUILD_OUT)/config/target
ifneq ($(wildcard $(TARGET_STAMP)),)
ifneq ($(strip $(shell cat $(TARGET_STAMP))),$(ARCH)-$(PLATFORM))
$(error BUILD_OUT target mismatch: $(BUILD_OUT))
endif
else
$(shell mkdir -p $(dir $(TARGET_STAMP)))
$(file >$(TARGET_STAMP),$(ARCH)-$(PLATFORM))
endif

SELECTION_STAMP = $(BUILD_OUT)/config/selection.flags
$(SELECTION_STAMP): private BUILD_OPTION = $(ARCH)-$(PLATFORM) $(CC) $(KERNEL_CFLAGS) $(foreach feature,$(KNOWN_FEATURE_VARIABLES),$(feature)=$($(feature)))
$(SELECTION_STAMP): .FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(BUILD_OPTION)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(C_KERNEL_OBJ) $(ASM_KERNEL_OBJ) $(GAS_KERNEL_OBJ): $(SELECTION_STAMP)
