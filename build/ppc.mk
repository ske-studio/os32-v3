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
INCLUDES := -I. -Iinclude -Ilib -Iarch/ppc -Iplatform/powermac
COMMON := -std=gnu11 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
 -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -O2 \
 -Wall -Wextra -Werror=vla -Werror=implicit-int -Werror=implicit-function-declaration
PPC_FLAGS := -D__cdecl= $(COMMON) $(INCLUDES) -mcpu=7450 -m32 -mbig-endian \
 -msoft-float -mno-altivec -msdata=none -nostdlib
HOST_FLAGS := $(COMMON) $(INCLUDES) -Itools/ppc -m32 -nostdlib -nostdinc \
 -isystem $(shell $(HOST_CC) -print-file-name=include) -no-pie
PRODUCT := platform/powermac/ofw.c platform/powermac/escc_tree.c \
 platform/powermac/escc.c platform/powermac/boot.c lib/kstring_c.c
ASSEMBLY := arch/ppc/entry.S arch/ppc/ofw_note.S
HEADERS := arch/ppc/ppc_boot.h platform/powermac/ppc_layout.h \
 platform/powermac/ofw.h lib/kstring.h include/types.h
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
$(OUT)/boot.ld: arch/ppc/boot.ld platform/powermac/ppc_layout.h
	@mkdir -p $(dir $@)
	$(PPC_CC) -E -P -x c -imacros platform/powermac/ppc_layout.h $< -o $@
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
MMU_C := platform/powermac/ofw.c platform/powermac/escc_tree.c \
 platform/powermac/escc.c platform/powermac/mmu_boot.c \
 platform/powermac/mmu_start.c platform/powermac/dump.c \
 arch/ppc/cpu_setup.c arch/ppc/pmap.c arch/ppc/exception.c arch/ppc/decrementer.c lib/kstring_c.c
MMU_S := arch/ppc/entry.S arch/ppc/ofw_note.S arch/ppc/takeover.S \
 arch/ppc/mmu.S arch/ppc/cache_copy.S arch/ppc/vectors.S arch/ppc/mmu_probe.S
MMU_HEADERS := $(HEADERS) $(wildcard arch/ppc/*.h platform/powermac/*.h)
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
$(OUT)/mmu.ld: arch/ppc/mmu.ld platform/powermac/ppc_layout.h
	@mkdir -p $(dir $@)
	$(PPC_CC) -E -P -x c -imacros platform/powermac/ppc_layout.h $< -o $@
$(OUT)/PPCMMU.ELF: $(MMU_OBJECTS) $(OUT)/mmu.ld mmu-force
	$(PPC_CC) $(PPC_FLAGS) -no-pie -Wl,-N -Wl,-m,elf32ppc \
	 -Wl,-T,$(OUT)/mmu.ld -Wl,-Map,$(OUT)/mmu.map $(MMU_OBJECTS) -o $@
$(OUT)/mmu-fake: tools/ppc/mmu_fake.c tools/ppc/mmu_fixture.h $(MMU_C) $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -DDIAGNOSTIC tools/ppc/mmu_fake.c lib/kstring_c.c -o $@

$(OUT)/mmu-flow: tools/ppc/mmu_flow_fake.c tools/ppc/flow_fixture.h $(MMU_C) $(MMU_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_FLAGS) -DPPC_MMU tools/ppc/mmu_flow_fake.c platform/powermac/mmu_boot.c \
	 platform/powermac/ofw.c platform/powermac/escc_tree.c platform/powermac/escc.c lib/kstring_c.c -o $@
