# Shared kernel object derivation and path-specific compiler flags.
C_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(C_KERNEL:.c=.o))
ASM_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o) $(ASM_EXTRA_OBJ))
GAS_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(GAS_KERNEL:.S=.o))

KERNEL_CFLAGS += $(DIAGNOSTIC_FLAG_SPLASH_DIAG_BUILD) $(DIAGNOSTIC_FLAG_SPLASH_OBS_BUILD) $(DIAGNOSTIC_FLAG_PCM_E4_DIAG_BUILD)
ifeq ($(TICK_PROBE_BUILD),1)
KERNEL_CFLAGS += -DTICK_PROBE -DSND2_REFILL_DIAG -DSND2_WRITER_DIAG
LAYOUT_CPP += -DTICK_PROBE
$(C_KERNEL_OBJ): KERNEL_CFLAGS += -DTICK_PROBE_IO
endif
.FORCE:

ifeq ($(ARCH),x86)
$(KERNEL_OBJ_PREFIX)sys/arch/x86/kernel/mtrr_wc.o: sys/arch/x86/kernel/mtrr_wc.h

# Test-only real guard injection: 0=off, 1=kernel, 2=SQLite engine.
DF_GUARD_TEST ?= 0
$(KERNEL_OBJ_PREFIX)sys/arch/x86/kernel/double_fault.o: private KERNEL_CFLAGS += -Os -DOS32_DF_GUARD_TEST=$(DF_GUARD_TEST)
$(KERNEL_OBJ_PREFIX)sys/arch/x86/kernel/double_fault.o: $(BUILD_OUT)/config/df-guard.flags

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
$(addprefix $(KERNEL_OBJ_PREFIX),sys/kern/kdebug/kdebug_module.o sys/kern/module/module_image.o sys/kern/module/module_runtime.o): private KERNEL_CFLAGS += -Os

# 常駐 shell の内部入口 (T7b-b2、sys/kern/exec/shell_service.c)。wrapper は
# __builtin_return_address(0) で呼び手を確かめるので、このファイルだけ
# 尾呼出しを封じて組む (設計 §3-2)。
SHELL_SERVICE_OBJ = $(KERNEL_OBJ_PREFIX)sys/kern/exec/shell_service.o
$(SHELL_SERVICE_OBJ): private KERNEL_CFLAGS += -fno-optimize-sibling-calls
# S3b から C_KERNEL に入る (結線は sys/kern/exec/exec.c)。旗が付くことは
# `make -n` で確かめる (tools/tests/test_shell_service.py の静的検査も見る)。

# T2h: entirely separate objects, dependencies, build ID, map and image.
R1_OUT := $(BUILD_OUT)/r1
R1_OBJ := $(R1_OUT)/obj
$(R1_OBJ)/sys/kern/exec/shell_service.o: private KERNEL_CFLAGS += -fno-optimize-sibling-calls
R1_C := $(C_KERNEL) sys/kern/r1_fixture.c sys/kern/font_guard_fixture.c
R1_OBJECTS := $(addprefix $(R1_OBJ)/,$(ASM_KERNEL:.asm=.o) arch/x86/kernel/ring3_ls_shim.o $(R1_C:.c=.o)) $(R1_OBJ)/build_id.o
R1_SMALL := sys/kern/exec/exec_heap.c sys/kern/exec/appmem.c sys/kern/exec/appmem_map.c sys/kern/exec/appmem_unmap.c sys/kern/mm/paging_app.c

$(R1_OBJ)/%.o: %.c | $(ABI_CONTRACT_ORDER)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(R1_SMALL)),-Os) -DOS32_R1_FIXTURE $(if $(filter sys/fs/fat/fatfs/%,$<),$(INC_FATFS),$(INC_KERNEL)) -c $< -o $@

# Preserve NASM's STT_FILE identity across H2's source move, including R1.
legacy_asm_input = $(subst /platform/pc98/legacy/v86_entry,/kernel/v86/v86_entry,$(1))

$(R1_OBJ)/sys/arch/x86/kernel/isr_stub.o $(KERNEL_OBJ_PREFIX)sys/arch/x86/kernel/isr_stub.o: sys/kern/time/tick_probe_ids.h

$(R1_OBJ)/%.o: %.asm sdk/crt/generations.inc sys/arch/x86/kernel/cpu_flags.inc sys/arch/x86/kernel/entry_layout.inc sys/arch/x86/kernel/iret_user.inc
	@mkdir -p $(@D) $(dir $(call legacy_asm_input,$@.asm))
	$(LAYOUT_CPP) -traditional-cpp $< -o $(call legacy_asm_input,$@.asm)
	$(AS) -p sdk/crt/generations.inc -f elf32 $(call legacy_asm_input,$@.asm) -o $@

$(R1_OBJ)/arch/x86/kernel/ring3_ls_shim.o: sys/arch/x86/kernel/ring3_ls.S sys/kern/exec/ring3_ls.h sdk/include/os32/os32_generations.h sys/include/cpu.h sys/include/abi.h sys/arch/x86/arch_abi.h sys/arch/x86/kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isys/arch/$(ARCH) -Isys/kern/exec -Isys/arch/x86/kernel -Isdk/include/os32 -c $< -o $@

$(R1_OUT)/build_id.c: .FORCE
	@mkdir -p $(@D)
	@python3 tools/gen_build_id.py -o $@

$(R1_OBJ)/build_id.o: $(R1_OUT)/build_id.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -DOS32_R1_FIXTURE -Iinclude -Isys/include -Isys/hal/time -Isys/hal/gfx -Isys/hal/input -Isys/platform/pc98 -Isys/hal/sound -Isys/arch/x86 -c $< -o $@

$(R1_OBJ)/sys/kern/exec/exec.o $(R1_OBJ)/sys/kapi/kapi_generated.o: sys/kapi/kapi_generated.c
$(R1_OBJ)/sys/dev/net/ne2000/lgy98.o: $(BUILD_OUT)/config/lgy98.flags
$(R1_OBJ)/sys/kapi/kapi_sysinfo.o: $(BUILD_OUT)/config/source-date.flags

endif

# Core objects always live under BUILD_OUT. Preserve each original -I order;
# the R1 fixture above deliberately keeps its existing INC_KERNEL profile.
LAYOUT_SMALL = $(R1_SMALL)
$(KERNEL_OBJ_PREFIX)%.o: private KERNEL_INCLUDES = $(INC_KERNEL)
$(KERNEL_OBJ_PREFIX)drivers/%.o: private KERNEL_INCLUDES = $(INC_DRIVERS)
$(KERNEL_OBJ_PREFIX)sys/hal/gfx/%.o: private KERNEL_INCLUDES = $(INC_GFX)
$(KERNEL_OBJ_PREFIX)sys/fs/%.o: private KERNEL_INCLUDES = $(INC_FS)
$(KERNEL_OBJ_PREFIX)sys/fs/fat/fatfs/%.o: private KERNEL_INCLUDES = $(INC_FATFS)
$(KERNEL_OBJ_PREFIX)sys/kern/exec/%.o: private KERNEL_INCLUDES = $(INC_EXEC)
$(KERNEL_OBJ_PREFIX)sys/kapi/%.o: private KERNEL_INCLUDES = $(INC_KAPI)
$(KERNEL_OBJ_PREFIX)lib/%.o $(addprefix $(KERNEL_OBJ_PREFIX),sys/kern/kprintf.o sys/kern/kprintf_attr.o sys/kern/kstring.o sys/kern/kstring_c.o): private KERNEL_INCLUDES = $(INC_LIB)
$(addprefix $(KERNEL_OBJ_PREFIX),sys/dev/video/cirrus/backend_cirrus.o sys/platform/pc98/video/cirrus/cirrus_identify.o sys/dev/video/geforce/backend_geforce.o sys/dev/video/geforce/gf_present.o sys/platform/pc98/video/backend_pc98.o sys/platform/pc98/video/backend_pegc.o sys/platform/pc98/video/pc98_route.o): private KERNEL_INCLUDES = $(INC_GFX)
CORE_KERNEL_INCLUDES = sys/dev/video/trident/trident_module.o sys/dev/video/trident/resources.o sys/dev/ata/cd_body.o sys/dev/ata/cd_module.o sys/dev/net/e82557/lan_body.o sys/dev/net/e82557/lan_module.o sys/dev/sound/cs4231/pcm_body.o sys/dev/sound/cs4231/pcm_module.o sys/dev/sound/fm/fm_body.o sys/dev/sound/fm/fm_module.o sys/dev/video/cirrus/cirrus_body.o sys/dev/video/cirrus/cirrus_module.o sys/dev/video/geforce/geforce_body.o sys/dev/video/geforce/geforce_module.o sys/dev/video/geforce/geforce_platform.o sys/dev/video/kcg/kcg_body.o sys/dev/video/kcg/kcg_module.o sys/platform/pc98/input/mouse.o sys/dev/block/loop_dev.o sys/dev/net/ne2000/ne2000.o sys/dev/net/ne2000/lgy98.o sys/dev/block/fdc.o sys/fs/serialfs/sfs_session_fg.o lib/sqlite3/os32_sqlite_test.o
$(addprefix $(KERNEL_OBJ_PREFIX),$(CORE_KERNEL_INCLUDES)): private KERNEL_INCLUDES = $(INC_KERNEL)

$(KERNEL_OBJ_PREFIX)%.o: %.c | $(ABI_CONTRACT_ORDER)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(LAYOUT_SMALL)),-Os) $(KERNEL_INCLUDES) -c $< -o $@

ifeq ($(ARCH),x86)
$(KERNEL_OBJ_PREFIX)arch/x86/kernel/ring3_ls_shim.o: sys/arch/x86/kernel/ring3_ls.S sys/kern/exec/ring3_ls.h sdk/include/os32/os32_generations.h sys/include/cpu.h sys/include/abi.h sys/arch/x86/arch_abi.h sys/arch/x86/kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isys/arch/$(ARCH) -Isys/kern/exec -Isys/arch/x86/kernel -Isdk/include/os32 -c $< -o $@

$(addprefix $(KERNEL_OBJ_PREFIX),sys/kern/exec/ring3_ls.o sys/kern/exec/exec.o): sys/kern/exec/ring3_ls.h
$(addprefix $(KERNEL_OBJ_PREFIX),$(filter sys/kern/exec/%.o sys/kapi/%.o,$(C_KERNEL:.c=.o))): sys/kapi/kapi_generated.c
$(KERNEL_OBJ_PREFIX)sys/dev/net/ne2000/lgy98.o: $(BUILD_OUT)/config/lgy98.flags
$(KERNEL_OBJ_PREFIX)sys/kapi/kapi_sysinfo.o: $(BUILD_OUT)/config/source-date.flags
$(KERNEL_OBJ_PREFIX)sys/fs/fat/fatfs/ff.o: sys/fs/fat/fatfs/ffconf.h sys/fs/fat/fatfs/string.h
$(KERNEL_OBJ_PREFIX)sys/dev/block/fat_diskio.o: sys/fs/fat/fatfs/ff.h sys/fs/fat/fatfs/diskio.h

$(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o)): $(KERNEL_OBJ_PREFIX)%.o: %.asm sdk/crt/generations.inc sys/arch/x86/kernel/cpu_flags.inc sys/arch/x86/kernel/entry_layout.inc sys/arch/x86/kernel/iret_user.inc
	@mkdir -p $(@D) $(dir $(call legacy_asm_input,$(BUILD_OUT)/asm/$<))
	$(LAYOUT_CPP) -traditional-cpp $< -o $(call legacy_asm_input,$(BUILD_OUT)/asm/$<)
	$(AS) -p sdk/crt/generations.inc -f elf32 $(call legacy_asm_input,$(BUILD_OUT)/asm/$<) -o $@

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
$(SELECTION_STAMP): private BUILD_OPTION = $(ARCH)-$(PLATFORM) IMAGE=$(IMAGE) $(CC) $(KERNEL_CFLAGS) $(foreach feature,$(KNOWN_FEATURE_VARIABLES),$(feature)=$($(feature)))
$(SELECTION_STAMP): .FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(BUILD_OPTION)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(C_KERNEL_OBJ) $(ASM_KERNEL_OBJ) $(GAS_KERNEL_OBJ): $(SELECTION_STAMP)
