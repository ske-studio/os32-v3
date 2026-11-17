# PowerPC core-ready kernel (milestone 2, docs/decisions/ppc/PPC_KBOOT_DESIGN.md).
# Independent of the x86 graph: explicit source manifest = the closure reached
# from init/ppc_kernel.c plus the takeover/vector/CPU entry objects. No libc,
# no libgcc; undefined symbols are a build failure. x86 objects are untouched.
#   make -f build/ppc-kboot.mk -> build/out/ppc-kboot/link/PPCKERN.ELF
#   make -f build/ppc-kboot.mk KBOOT_LINK=0 NEGATIVE=1 -> build/out/ppc-kboot/neg1/PPCKERN.ELF
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
KB_CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
KB_OBJCOPY := $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
KB_NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
KB_OBJDUMP := $(PPC_CROSS_DIR)/bin/powerpc-elf-objdump
NEGATIVE ?= 0
KBOOT_SHELL ?= 0
KBOOT_SH ?= 0
KBOOT_LINK ?= 1
ifeq ($(KBOOT_LINK),1)
ifeq ($(KBOOT_SHELL),1)
$(error KBOOT_LINK requires the diagnostic image)
endif
ifneq ($(NEGATIVE),0)
$(error KBOOT_LINK does not support NEGATIVE)
endif
endif
ifeq ($(KBOOT_SH),1)
ifeq ($(KBOOT_SHELL),1)
$(error KBOOT_SH and KBOOT_SHELL are mutually exclusive)
endif
ifneq ($(NEGATIVE),0)
$(error KBOOT_SH does not support NEGATIVE)
endif
endif
KBOOT_TERM ?= lan
KB_FD1_STREAM ?= $(KBOOT_SH)
# Extra applications belong to an explicitly separate test image.
KB_ROOT_BINS ?= $(KB_SH_OUT)/hello.bin $(KB_SH_OUT)/fault.bin
KB_EXEC := $(filter 1,$(KBOOT_SHELL) $(KBOOT_SH))
KB_OUT ?= build/out/ppc-kboot$(if $(filter 1,$(KBOOT_LINK)),/$(if $(filter 1,$(KBOOT_SH)),link-sh,link),$(if $(filter 1,$(KBOOT_SHELL)),/shell,$(if $(filter 1,$(KBOOT_SH)),/sh,$(if $(filter-out 0,$(NEGATIVE)),/neg$(NEGATIVE)))))
ifeq ($(KB_FD1_STREAM)$(KBOOT_SH),10)
KB_OUT := $(KB_OUT)/fd1-stream
endif
KB_ELF := $(KB_OUT)/PPCKERN.ELF
.DEFAULT_GOAL := all

ROOT_ISO := $(KB_OUT)/root.iso
ROOT_BLOB := $(KB_OUT)/root_image_blob.o
ROOT_MOTD := $(KB_OUT)/motd.txt
KB_SH_OUT := build/out/ppc-user$(if $(filter 1,$(KBOOT_LINK)),/link-sh)
ROOT_BINS = $(KB_ROOT_BINS) $(if $(filter 1,$(KBOOT_SH)),$(KB_SH_OUT)/sh.bin)
ROOT_OBJCOPY := $(KB_OBJCOPY)
ROOT_ELF_FORMAT := elf32-powerpc
ROOT_CPU := powerpc
ROOT_MANIFEST ?= build/ppc-ram-root.json
ROOT_DEPS ?= assets/fonts/shino16.kcgfont assets/fonts/JF-Dot-Shinonome16-LICENSE.txt build/out/unicode.bin
include build/font-fetch.mk

build/out/unicode.bin: tools/gen_unicode.c lib/unicode_jis_table.h kernel/console/unicode_file.h include/endian_le.h include/types.h include/callconv.h
	@mkdir -p $(dir $@)
	gcc -std=gnu11 -O2 -I. -Iinclude tools/gen_unicode.c -o $(dir $@)gen_unicode
	$(dir $@)gen_unicode --pairs $@

include build/ram-root.mk

# Sources: owner sets and link order live in build/select.mk (docs/design/build/select.md).
# The flags choose the image; this file keeps only the PowerPC product rules.
ARCH := ppc
PLATFORM := powermac
IMAGE := $(if $(filter 1,$(KBOOT_SH)),$(if $(filter 1,$(KBOOT_LINK)),kboot-link-sh,kboot-sh),$(if $(filter 1,$(KBOOT_SHELL)),kboot-shell,$(if $(filter 1,$(KBOOT_LINK)),kboot-link,kboot-core)))
include build/select.mk
# Entry objects first (kernel.ld keeps .text.entry at the image start).
KB_ASM := $(GAS_KERNEL)
KB_C := $(C_KERNEL)
ifeq ($(KBOOT_LINK),1)
KB_INC_EXTRA += -Inet
endif
ifneq ($(KB_EXEC),)
ifneq ($(KBOOT_TERM),lan)
$(error KBOOT_TERM must be lan)
endif
ifneq ($(filter-out $(KB_C),$(RAM_ROOT_C)),)
$(error RAM root sources are not selected: $(filter-out $(KB_C),$(RAM_ROOT_C)))
endif
KB_EXTRA := $(KB_OUT)/pm_log_printf.o $(KB_OUT)/root_image_blob.o $(KB_OUT)/gen/kapi/kapi_generated.o
KB_GEN := $(KB_OUT)/gen
KB_INC_EXTRA += -I$(KB_GEN)/include -I$(KB_GEN)/sdk/include/os32 \
 -Ikapi -Idrivers -Inet -Igfx -Iexec \
 -Idrivers/char -Idrivers/net -Idrivers/dma -Idrivers/input -Idrivers/pci \
 -Idrivers/sound -Idrivers/video -Ifs/fat -Ifs/ext2 -Ifs/ramfs \
 -Ifs/serialfs -Ifs/hostdrv -Ikernel/module -Ikernel/v86 -Ikernel/ime \
 -Igfx/sync -Igfx/v2 -Idrivers/video/geforce -Idrivers/video/kcg
KB_INC_EXTRA += -Idrivers/ata -Idrivers/base -Ifs -Ifs/isofs -Idrivers/block -Ikernel/cli
endif
KB_OBJ := $(addprefix $(KB_OUT)/,$(KB_ASM:.S=.o) $(KB_C:.c=.o))

# Same machine flags as build/arch/ppc.mk:8 for every kernel TU (design section 4).
KB_MACHINE := -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none \
 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon -fsigned-char \
 -fno-short-enums
KB_INC := $(if $(KB_EXEC),-I$(KB_GEN)/include -I$(KB_GEN)/sdk/include/os32) -I$(KB_OUT) -I. -Iinclude -Iarch/ppc -Iplatform/powermac -Isdk/include \
 -Isdk/include/os32 -Ikernel -Iinit -Ikernel/console -Ikernel/irq -Ikernel/time \
 -Imm -Ilib $(KB_INC_EXTRA)
KB_FLAGS := -std=gnu11 $(KB_MACHINE) -fno-builtin -fno-asynchronous-unwind-tables \
 -O2 -Wall -Wextra -Werror -Werror=vla -Werror=implicit-int \
 -Werror=implicit-function-declaration -ffunction-sections -fdata-sections \
 -D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=$(NEGATIVE) -DKBOOT_SHELL=$(KBOOT_SHELL) -DKBOOT_SH=$(KBOOT_SH) -DKBOOT_LINK=$(KBOOT_LINK) -MMD -MP $(KB_INC)

.PHONY: all audit
all: $(KB_ELF) audit

$(KB_OUT)/memmap_layout.h: include/memmap.h tools/gen_memmap.py
	@mkdir -p $(dir $@)
	python3 -B tools/gen_memmap.py --layout-header $@
ifneq ($(KB_EXEC),)
$(KB_GEN)/stamp: sdk/kapi.json sdk/gen_kapi.py sdk/kapi_abi_x86.py sdk/kapi_target_ppc.py sdk/target_profile.py
	@mkdir -p $(KB_GEN)
	python3 -B sdk/gen_kapi.py --target ppc-powermac --out-dir $(KB_GEN)
	@touch $@
$(KB_OBJ): $(KB_GEN)/stamp
$(KB_OUT)/gen/kapi/kapi_generated.o: $(KB_GEN)/stamp $(KB_OUT)/memmap_layout.h build/ppc-kboot.mk
	$(KB_CC) $(KB_FLAGS) -c $(KB_GEN)/kapi/kapi_generated.c -o $@
-include $(KB_OUT)/gen/kapi/kapi_generated.d
endif
# Mutation testing (tools/ppc/kboot_accept.py): compile KB_MUT_FILE in place of
# the product source KB_MUT_SRC, into a separate KB_OUT. Never set otherwise.
KB_MUT_SRC ?=
KB_MUT_FILE ?=
# A serial input substitution may accompany one product mutation.
KB_MUT_EXTRA_SRC ?=
KB_MUT_EXTRA_FILE ?=
KB_MUT_THIRD_SRC ?=
KB_MUT_THIRD_FILE ?=
kb_src = $(if $(and $(KB_MUT_SRC),$(filter $(KB_MUT_SRC),$1)),$(KB_MUT_FILE) -I$(dir $1),$(if $(filter $(KB_MUT_EXTRA_SRC),$1),$(KB_MUT_EXTRA_FILE) -I$(dir $1),$(if $(filter $(KB_MUT_THIRD_SRC),$1),$(KB_MUT_THIRD_FILE) -I$(dir $1),$1)))
$(KB_OUT)/lib/lz4.o: KB_TU_FLAGS += -O0
ifneq ($(KB_EXEC),)
$(KB_OUT)/platform/powermac/usb_platform.o: KB_TU_FLAGS += -Dkvprintf=pm_log_kvprintf
$(KB_OUT)/platform/powermac/cli_boot.o: KB_TU_FLAGS += -Dconsole_stream_write=pm_cli_write
# Existing boot diagnostics retain their LAN LOG channel. Terminal kprintf
# still uses lib/kprintf.o and the ordinary console mirror.
$(addprefix $(KB_OUT)/,$(filter init/ppc_kernel.o arch/ppc/%.o platform/powermac/%.o drivers/%.o gfx/%.o mm/%.o fs/%.o kernel/%.o,$(KB_C:.c=.o))): KB_TU_FLAGS += -Dkprintf=pm_log_printf
$(KB_OUT)/pm_log_printf.o: lib/kprintf.c build/ppc-kboot.mk $(KB_GEN)/stamp $(KB_OUT)/memmap_layout.h
	$(KB_CC) $(KB_FLAGS) -Dkprintf=pm_log_printf -Dkvprintf=pm_log_kvprintf \
	 -Dkutoa_dec=pm_log_dec -Dkutoa_hex=pm_log_hex -Dkprintf_spec=pm_log_spec \
	 -Dconsole_write=pm_diag_write -Dshell_print=pm_diag_print \
	 -Dshell_print_utf8=pm_diag_print_utf8 -c $< -o $@
endif
$(KB_OUT)/kernel/console/console.o: KB_TU_FLAGS = -Wno-sign-compare -DCONSOLE_PUTCHAR_MIRROR=1 -Dshell_print_utf8=pm_console_legacy_print_utf8
$(KB_OUT)/fs/vfs_fd.o: KB_TU_FLAGS = $(if $(filter 1,$(KB_FD1_STREAM)),-Dconsole_write=pm_fd1_write)
$(KB_OUT)/%.o: %.c $(KB_OUT)/memmap_layout.h build/ppc-kboot.mk $(KB_MUT_FILE) $(KB_MUT_EXTRA_FILE) $(KB_MUT_THIRD_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) $(KB_TU_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/%.o: %.S $(KB_OUT)/memmap_layout.h build/ppc-kboot.mk $(KB_MUT_FILE) $(KB_MUT_EXTRA_FILE) $(KB_MUT_THIRD_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/kernel.ld: arch/ppc/kernel.ld platform/powermac/kboot_layout.h $(KB_OUT)/memmap_layout.h
	$(KB_CC) $(KB_FLAGS) -MF $@.d -E -P -x c $< -o $@
$(KB_OUT)/motd.txt:
	@mkdir -p $(dir $@)
	@printf 'OS32 PowerPC shell\n' > $@
.PHONY: kb-user
kb-user:
	$(MAKE) ARCH=ppc PLATFORM=powermac CROSS_DIR=$(PPC_CROSS_DIR) $(if $(filter 1,$(KBOOT_LINK)),PPC_USER_OUT=$(KB_SH_OUT) PPC_SH_PROFILE=userland/ppc/sh/link_profile.h) $(ROOT_BINS)

$(ROOT_BINS): kb-user


# kboot_tick is the acceptance meter's name for the logical 100 Hz tick_count (physical DEC is 1 kHz).
$(KB_ELF): $(KB_OBJ) $(KB_EXTRA) $(KB_OUT)/kernel.ld
	$(KB_CC) $(KB_FLAGS) -MF $@.d -no-pie -Wl,--gc-sections -Wl,--no-undefined \
	 -Wl,-z,noexecstack -Wl,--defsym,kboot_tick=tick_count -Wl,-T,$(KB_OUT)/kernel.ld \
	 -Wl,-Map,$(KB_ELF:.ELF=.map) $(KB_OBJ) $(KB_EXTRA) -o $@

# H3 audits (tools/ppc/kboot_audit.py): nothing unresolved, integer-only code by
# decoded mnemonic, and no direct or macro-mediated user of the 100 Hz PIT_HZ
# inside the PPC closure: common code uses OS32_TICK_HZ (design R7). Mutated sources are audited too.
# ISO media tick-rate debt is documented in PPC_SHELL_DESIGN §8; RAM media is fixed.
audit: $(KB_ELF)
	@python3 -B tools/ppc/kboot_audit.py --elf $(KB_ELF) --map $(KB_ELF:.ELF=.map) --objects $(KB_OBJ) $(KB_EXTRA) --fp-object $(KB_OUT)/arch/ppc/fp_context.o --nm $(KB_NM) --objdump $(KB_OBJDUMP) \
	 --sources $(filter-out fs/isofs/iso9660.c,$(KB_C)) $(KB_ASM) $(if $(KB_MUT_SRC),--mutant $(KB_MUT_SRC) $(KB_MUT_FILE)) $(if $(KB_MUT_EXTRA_SRC),--mutant $(KB_MUT_EXTRA_SRC) $(KB_MUT_EXTRA_FILE)) $(if $(KB_MUT_THIRD_SRC),--mutant $(KB_MUT_THIRD_SRC) $(KB_MUT_THIRD_FILE)) \
	 -- $(KB_CC) $(KB_FLAGS)

-include $(KB_OBJ:.o=.d) $(if $(KB_EXEC),$(KB_OUT)/pm_log_printf.d)
