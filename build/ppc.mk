# Independent PowerPC bootstrap; no x86 config, generated KAPI, libc or libgcc.
CROSS_DIR ?= /home/hight/opt/cross-ppc
PPC_CC := $(CROSS_DIR)/bin/powerpc-elf-gcc
HOST_CC ?= gcc
HOST32_RUNNERS ?= qemu
OUT ?= build/out/ppc
ELF := $(OUT)/PPCBOOT.ELF
ISO := $(OUT)/boot.iso
SERIAL_A := $(OUT)/a.log
SERIAL_B := $(OUT)/b.log
QMP := $(OUT)/qmp.sock
INCLUDES := -I. -Iinclude -Isys/include -Isys/hal/time -Isys/hal/gfx -Isys/hal/input -Isys/platform/pc98 -Isys/hal/sound -Isys/arch/x86 -Ilib -Isys/kern -Iarch/ppc -Isys/arch/ppc -Iplatform/powermac -Isys/platform/powermac
COMMON := -std=gnu11 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
 -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -O2 \
 -Wall -Wextra -Werror=vla -Werror=implicit-int -Werror=implicit-function-declaration
PPC_FLAGS := -D__cdecl= $(COMMON) $(INCLUDES) -mcpu=7450 -m32 -mbig-endian \
 -msoft-float -mno-altivec -msdata=none -nostdlib
HOST_FLAGS := $(COMMON) $(INCLUDES) -Itools/ppc -Iuserland/diag -m32 -nostdlib -nostdinc \
 -isystem $(shell $(HOST_CC) -print-file-name=include) -no-pie
PRODUCT := sys/platform/powermac/rh_observe.c sys/platform/powermac/ofw.c sys/platform/powermac/escc_tree.c \
 sys/platform/powermac/escc.c sys/platform/powermac/boot.c sys/kern/kstring_c.c
ASSEMBLY := sys/arch/ppc/entry.S sys/arch/ppc/ofw_note.S
HEADERS := sys/arch/ppc/ppc_boot.h sys/platform/powermac/platform_of_layout.h \
 sys/platform/powermac/ofw.h sys/kern/kstring.h sys/include/types.h
OBJECTS := $(addprefix $(OUT)/,$(PRODUCT:.c=.o) $(ASSEMBLY:.S=.o))
.DEFAULT_GOAL := all
.PHONY: all host check paths
all: $(ELF) $(OUT)/flow mmu
host: $(OUT)/flow
$(OUT)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(PPC_FLAGS) -c $< -o $@
$(OUT)/%.o: %.S $(HEADERS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(PPC_FLAGS) -c $< -o $@
$(OUT)/boot.ld: sys/arch/ppc/boot.ld sys/platform/powermac/platform_of_layout.h
	@mkdir -p $(dir $@)
	$(PPC_CC) -E -P -x c -imacros sys/platform/powermac/platform_of_layout.h $< -o $@
$(ELF): $(OBJECTS) $(OUT)/boot.ld
	$(PPC_CC) $(PPC_FLAGS) -no-pie -Wl,-N -Wl,-m,elf32ppc \
	 -Wl,-T,$(OUT)/boot.ld -Wl,-Map,$(OUT)/boot.map $(OBJECTS) -o $@
$(OUT)/flow: tools/ppc/flow_fake.c tools/ppc/flow_fixture.h $(PRODUCT) $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) tools/ppc/flow_fake.c $(PRODUCT) -o $@
# All path derivations belong here; the runner obtains them via this target.
paths:
	@echo $(OUT) $(ELF) $(ISO) $(SERIAL_A) $(SERIAL_B) $(QMP)
check: all
	python3 tools/ppc/run.py --cross-dir $(CROSS_DIR) --runner $(HOST32_RUNNERS)

# Independent MMU image; old bootstrap objects and acceptance remain separate.
MMU_C := sys/platform/powermac/rh_observe.c sys/platform/powermac/ofw.c sys/platform/powermac/escc_tree.c \
 sys/platform/powermac/escc.c sys/platform/powermac/mmu_boot.c \
 sys/platform/powermac/mmu_start.c sys/platform/powermac/dump.c \
 sys/arch/ppc/cpu_setup.c sys/arch/ppc/pmap.c sys/arch/ppc/exception.c sys/arch/ppc/decrementer.c sys/kern/kstring_c.c
MMU_S := sys/arch/ppc/entry.S sys/arch/ppc/ofw_note.S sys/arch/ppc/takeover.S \
 sys/arch/ppc/mmu.S sys/arch/ppc/cache_copy.S sys/arch/ppc/vectors.S sys/arch/ppc/mmu_probe.S
MMU_HEADERS := $(HEADERS) sys/arch/ppc/arch_abi.h sys/arch/ppc/arch_copy.h sys/arch/ppc/arch_cpu.h sys/arch/ppc/arch_diag_stack.h sys/arch/ppc/arch_endian_dma.h sys/arch/ppc/arch_exec_cache.h sys/arch/ppc/arch_exec_image.h sys/arch/ppc/arch_exec_kapi.h sys/arch/ppc/arch_exec_selftest.h sys/arch/ppc/arch_exec_storage.h sys/arch/ppc/arch_io.h sys/arch/ppc/arch_jmp_storage.h sys/arch/ppc/arch_kernel_fp.h sys/arch/ppc/arch_tick_budget.h sys/arch/ppc/arch_type_contract.h sys/arch/ppc/kapi_table.h sys/arch/ppc/kernel_boot.h sys/arch/ppc/kernel_entry.h sys/arch/ppc/kernel_fp.h sys/arch/ppc/kernel_probe.h sys/arch/ppc/ppc_boot.h sys/arch/ppc/ppc_frame.h sys/arch/ppc/ppc_hpt.h sys/arch/ppc/ppc_mmio16.h sys/arch/ppc/ppc_oea.h sys/arch/ppc/ppc_pd.h sys/arch/ppc/ppc_pmap.h sys/arch/ppc/ppc_trap.h sys/platform/powermac/apm.h sys/platform/powermac/cli_boot.h sys/platform/powermac/diag_slots.h sys/platform/powermac/display_text.h sys/platform/powermac/exec_boot.h sys/platform/powermac/hostfs.h sys/platform/powermac/hwb_instructions.h sys/platform/powermac/hwb_resource.h sys/platform/powermac/hwb_window.h sys/platform/powermac/ide_boot.h sys/platform/powermac/ide_compatible.h sys/platform/powermac/input_boot.h sys/platform/powermac/input_usb.h sys/platform/powermac/install_apm.h sys/platform/powermac/install_backend.h sys/platform/powermac/kboot_info.h sys/platform/powermac/kboot_log.h sys/platform/powermac/kboot_report.h sys/platform/powermac/kdebug_imports.h sys/platform/powermac/lan_boot.h sys/platform/powermac/link_platform.h sys/platform/powermac/loop_boot.h sys/platform/powermac/memory.h sys/platform/powermac/mpic.h sys/platform/powermac/mpic_tree.h sys/platform/powermac/nic_gem.h sys/platform/powermac/ofw.h sys/platform/powermac/pci_capture.h sys/platform/powermac/platform_atapi_bus.h sys/platform/powermac/platform_build_info.h sys/platform/powermac/platform_console.h sys/platform/powermac/platform_display_fallback.h sys/platform/powermac/platform_ext2.h sys/platform/powermac/platform_handoff_layout.h sys/platform/powermac/platform_hwb.h sys/platform/powermac/platform_hwb_addr.h sys/platform/powermac/platform_hwb_pci.h sys/platform/powermac/platform_hwb_session.h sys/platform/powermac/platform_ide_bus.h sys/platform/powermac/platform_ime_render.h sys/platform/powermac/platform_input_caps.h sys/platform/powermac/platform_install_layout.h sys/platform/powermac/platform_install_management.h sys/platform/powermac/platform_io.h sys/platform/powermac/platform_irq_limits.h sys/platform/powermac/platform_kboot_layout.h sys/platform/powermac/platform_kprintf.h sys/platform/powermac/platform_link_inline.h sys/platform/powermac/platform_machcfg.h sys/platform/powermac/platform_mem_stat.h sys/platform/powermac/platform_memory_desc.h sys/platform/powermac/platform_of_layout.h sys/platform/powermac/platform_osri.h sys/platform/powermac/platform_paging_window.h sys/platform/powermac/platform_usb_identity.h sys/platform/powermac/platform_ver.h sys/platform/powermac/pm_console.h sys/platform/powermac/platform_ppc_memmap.h sys/platform/powermac/product_boot.h sys/platform/powermac/rh_observe.h sys/platform/powermac/root_boot.h sys/platform/powermac/shell_boot.h sys/platform/powermac/shell_loop_ops.h sys/platform/powermac/usb_boot.h sys/platform/powermac/via_window.h
MMU_STAGE ?= 3
MMU_MODE ?= 0
MMU_DIR := $(OUT)/mmu-$(MMU_STAGE)-$(MMU_MODE)
MMU_OBJECTS := $(addprefix $(MMU_DIR)/,$(MMU_C:.c=.o) $(MMU_S:.S=.o))
.PHONY: mmu mmu-force
mmu-force:
mmu: $(OUT)/PPCMMU.ELF
$(MMU_DIR)/%.o: %.c $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(PPC_FLAGS) -DDIAGNOSTIC -DPPC_MMU -DPPC_MMU_STAGE=$(MMU_STAGE) -DPPC_MMU_MODE=$(MMU_MODE) -c $< -o $@
$(MMU_DIR)/%.o: %.S $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(PPC_FLAGS) -DDIAGNOSTIC -DPPC_MMU -DPPC_MMU_STAGE=$(MMU_STAGE) -DPPC_MMU_MODE=$(MMU_MODE) -c $< -o $@
$(OUT)/mmu.ld: sys/arch/ppc/mmu.ld sys/platform/powermac/platform_of_layout.h
	@mkdir -p $(dir $@)
	$(PPC_CC) -E -P -x c -imacros sys/platform/powermac/platform_of_layout.h $< -o $@
$(OUT)/PPCMMU.ELF: $(MMU_OBJECTS) $(OUT)/mmu.ld mmu-force
	$(PPC_CC) $(PPC_FLAGS) -no-pie -Wl,-N -Wl,-m,elf32ppc \
	 -Wl,-T,$(OUT)/mmu.ld -Wl,-Map,$(OUT)/mmu.map $(MMU_OBJECTS) -o $@
$(OUT)/mmu-fake: tools/ppc/mmu_fake.c tools/ppc/mmu_fixture.h $(MMU_C) $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -DDIAGNOSTIC tools/ppc/mmu_fake.c sys/kern/kstring_c.c -o $@

$(OUT)/mmu-flow: tools/ppc/mmu_flow_fake.c tools/ppc/flow_fixture.h $(MMU_C) $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -DPPC_MMU tools/ppc/mmu_flow_fake.c sys/platform/powermac/mmu_boot.c \
	 sys/platform/powermac/rh_observe.c sys/platform/powermac/ofw.c sys/platform/powermac/escc_tree.c sys/platform/powermac/escc.c sys/kern/kstring_c.c -o $@
