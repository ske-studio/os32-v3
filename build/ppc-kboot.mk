# PowerPC core-ready kernel (milestone 2, docs/decisions/ppc/PPC_KBOOT_DESIGN.md).
# Independent of the x86 graph: explicit source manifest = the closure reached
# from init/ppc_kernel.c plus the takeover/vector/CPU entry objects. No libc,
# no libgcc; undefined symbols are a build failure. x86 objects are untouched.
#   make -f build/ppc-kboot.mk            -> build/out/ppc-kboot/PPCKERN.ELF
#   make -f build/ppc-kboot.mk NEGATIVE=1 -> build/out/ppc-kboot/neg1/PPCKERN.ELF
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
KB_CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
KB_NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
KB_OBJDUMP := $(PPC_CROSS_DIR)/bin/powerpc-elf-objdump
NEGATIVE ?= 0
KB_OUT ?= build/out/ppc-kboot$(if $(filter-out 0,$(NEGATIVE)),/neg$(NEGATIVE))
KB_ELF := $(KB_OUT)/PPCKERN.ELF

# Entry objects first (kernel.ld keeps .text.entry at the image start).
KB_ASM := arch/ppc/kernel_entry.S arch/ppc/entry.S arch/ppc/kernel_takeover.S \
 arch/ppc/kernel_vectors.S arch/ppc/kernel_mmu.S arch/ppc/kernel_probe.S \
 arch/ppc/mmu.S arch/ppc/cache_copy.S
KB_ARCH := arch/ppc/kernel_boot.c arch/ppc/kernel_cpu.c arch/ppc/kernel_hpt.c \
 arch/ppc/kernel_trap.c arch/ppc/kernel_dec.c arch/ppc/pd_walk.c \
 arch/ppc/kboot_unavailable.c
KB_PLATFORM := platform/powermac/ofw.c platform/powermac/escc.c \
 platform/powermac/escc_tree.c platform/powermac/kboot_capture.c \
 platform/powermac/kboot_reserve.c platform/powermac/memory.c \
 platform/powermac/mpic_tree.c platform/powermac/mpic.c \
 platform/powermac/clock.c platform/powermac/log.c \
 platform/powermac/kboot_console.c
# Common kernel code, compiled unmodified.
KB_COMMON := init/ppc_kernel.c mm/memory_boot.c mm/paging.c mm/physmem.c \
 mm/pgalloc.c mm/kmalloc.c mm/shm.c kernel/irq/tick.c kernel/time/tick.c \
 lib/kprintf.c lib/kstring_c.c lib/divdi3.c kernel/sys.c fs/fd_redirect.c
KB_C := $(KB_ARCH) $(KB_PLATFORM) $(KB_COMMON)
KB_OBJ := $(addprefix $(KB_OUT)/,$(KB_ASM:.S=.o) $(KB_C:.c=.o))

# Same machine flags as build/arch/ppc.mk:8 for every kernel TU (design section 4).
KB_MACHINE := -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none \
 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon -fsigned-char \
 -fno-short-enums
KB_INC := -I$(KB_OUT) -I. -Iinclude -Iarch/ppc -Iplatform/powermac -Isdk/include \
 -Isdk/include/os32 -Ikernel -Iinit -Ikernel/console -Ikernel/irq -Ikernel/time \
 -Imm -Ilib
KB_FLAGS := -std=gnu11 $(KB_MACHINE) -fno-builtin -fno-asynchronous-unwind-tables \
 -O2 -Wall -Wextra -Werror -Werror=vla -Werror=implicit-int \
 -Werror=implicit-function-declaration -ffunction-sections -fdata-sections \
 -D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=$(NEGATIVE) -MMD -MP $(KB_INC)

.DEFAULT_GOAL := all
.PHONY: all audit
all: $(KB_ELF) audit

$(KB_OUT)/memmap_layout.h: include/memmap.h tools/gen_memmap.py
	@mkdir -p $(dir $@)
	python3 -B tools/gen_memmap.py --layout-header $@
# Mutation testing (tools/ppc/kboot_accept.py): compile KB_MUT_FILE in place of
# the product source KB_MUT_SRC, into a separate KB_OUT. Never set otherwise.
KB_MUT_SRC ?=
KB_MUT_FILE ?=
kb_src = $(if $(and $(KB_MUT_SRC),$(filter $(KB_MUT_SRC),$1)),$(KB_MUT_FILE) -I$(dir $1),$1)
$(KB_OUT)/%.o: %.c $(KB_OUT)/memmap_layout.h build/ppc-kboot.mk $(KB_MUT_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/%.o: %.S $(KB_OUT)/memmap_layout.h build/ppc-kboot.mk $(KB_MUT_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/kernel.ld: arch/ppc/kernel.ld platform/powermac/kboot_layout.h $(KB_OUT)/memmap_layout.h
	$(KB_CC) $(KB_FLAGS) -MF $@.d -E -P -x c $< -o $@
# kboot_tick is the acceptance meter's name for the live 1 kHz tick counter.
$(KB_ELF): $(KB_OBJ) $(KB_OUT)/kernel.ld
	$(KB_CC) $(KB_FLAGS) -MF $@.d -no-pie -Wl,--gc-sections -Wl,--no-undefined \
	 -Wl,-z,noexecstack -Wl,--defsym,kboot_tick=tick_count -Wl,-T,$(KB_OUT)/kernel.ld \
	 -Wl,-Map,$(KB_ELF:.ELF=.map) $(KB_OBJ) -o $@

# H3 audits (tools/ppc/kboot_audit.py): nothing unresolved, integer-only code by
# decoded mnemonic, and no direct or macro-mediated user of the 100 Hz PIT_HZ
# inside a kernel that ticks at 1 kHz (design R7). Mutated sources are audited too.
audit: $(KB_ELF)
	@python3 -B tools/ppc/kboot_audit.py --elf $(KB_ELF) --nm $(KB_NM) --objdump $(KB_OBJDUMP) \
	 --sources $(KB_C) $(KB_ASM) $(if $(KB_MUT_SRC),--mutant $(KB_MUT_SRC) $(KB_MUT_FILE)) \
	 -- $(KB_CC) $(KB_FLAGS)

-include $(KB_OBJ:.o=.d)
