# PowerPC (PowerMac) の製品の規則。最上位の Makefile が ARCH=ppc PLATFORM=powermac の時に読む。
#   make ARCH=ppc PLATFORM=powermac [IMAGE=kboot-*] [kernel]       -> build/out/ppc-kboot/<像>/PPCKERN.ELF
#   make ARCH=ppc PLATFORM=powermac KBOOT_SH=1 KBOOT_LINK=0 …      (道具は旗でも像を選べる)
#   make ARCH=ppc PLATFORM=powermac KBOOT_LINK=0 NEGATIVE=1        -> build/out/ppc-kboot/neg1/PPCKERN.ELF
# ソースは x86 と同じ所有者集合 (build/select.mk) から像の構成で選ぶ。像の選び方は
# build/images/ppc-flags.mk。libc・libgcc は使わず、未解決のシンボルはビルドの失敗。
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
KB_CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
KB_OBJCOPY := $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
KB_NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
KB_OBJDUMP := $(PPC_CROSS_DIR)/bin/powerpc-elf-objdump
KBOOT_TERM ?= lan
# Extra applications belong to an explicitly separate test image.
KB_ROOT_BINS ?= $(KB_SH_OUT)/hello.bin $(KB_SH_OUT)/fault.bin
KB_EXEC := $(filter 1,$(KBOOT_SHELL) $(KBOOT_SH))
KB_OUT ?= build/out/ppc-kboot$(if $(filter 1,$(KBOOT_LINK)),/$(if $(filter 1,$(KBOOT_SH)),link-sh,link),$(if $(filter 1,$(KBOOT_SHELL)),/shell,$(if $(filter 1,$(KBOOT_SH)),/sh,$(if $(filter-out 0,$(NEGATIVE)),/neg$(NEGATIVE)))))
KB_ELF := $(KB_OUT)/PPCKERN.ELF

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
KB_EXTRA := $(KB_OUT)/root_image_blob.o $(KB_OUT)/gen/kapi/kapi_generated.o
KB_GEN := $(KB_OUT)/gen
KB_INC_EXTRA += -I$(KB_GEN)/include -I$(KB_GEN)/sdk/include/os32 \
 -Ikapi -Idrivers -Inet -Igfx -Iexec \
 -Idrivers/char -Idrivers/net -Idrivers/dma -Idrivers/input -Idrivers/pci \
 -Idrivers/sound -Idrivers/video -Ifs/fat -Ifs/ext2 -Ifs/ramfs \
 -Ifs/serialfs -Ikernel/module -Ikernel/v86 -Ikernel/ime \
 -Igfx/sync -Igfx/v2 -Idrivers/video/geforce -Idrivers/video/kcg
KB_INC_EXTRA += -Idrivers/ata -Ikernel/dev -Ifs -Ifs/isofs -Idrivers/block -Ikernel/cli
endif
KB_OBJ := $(addprefix $(KB_OUT)/,$(KB_ASM:.S=.o) $(KB_C:.c=.o))
# The source lists and link order live in these files: a change there relinks the image.
KB_SELECT_MK := build/select.mk build/sources/common.mk build/sources/order.mk build/sources/hooks.mk \
 build/arch/ppc-sources.mk build/platform/powermac.mk build/images/$(IMAGE).mk
$(KB_OBJ) $(KB_ELF): $(KB_SELECT_MK)

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
 -D__cdecl= -D__KERNEL_BUILD__ $(HOOK_DEFS) -DKBOOT_NEGATIVE=$(NEGATIVE) -DKBOOT_SHELL=$(KBOOT_SHELL) -DKBOOT_SH=$(KBOOT_SH) -DKBOOT_LINK=$(KBOOT_LINK) -MMD -MP $(KB_INC)

.PHONY: all kernel audit
all: kernel
kernel: $(KB_ELF) audit
# 共通の置き場のソースは、PowerPC の像にまだ入っていない物 (C_COMMON_PC98_PENDING) も PowerPC の
# コンパイラを通す。機種の名前 (PC-98 の定数など) を間接に使っていれば、ここで未宣言になって止まる。
# リンクはしない (像には入れない)。
KB_PENDING_OBJ := $(addprefix $(KB_OUT)/,$(patsubst %.c,%.o,$(filter %.c,$(C_COMMON_PC98_PENDING))))
$(KB_OUT)/kapi/kapi_db.o: KB_TU_FLAGS += -Ilib/sqlite3
.PHONY: common-compile
common-compile: $(KB_PENDING_OBJ)

$(KB_OUT)/memmap_layout.h: include/memmap.h tools/gen_memmap.py
	@mkdir -p $(dir $@)
	python3 -B tools/gen_memmap.py --layout-header $@
ifneq ($(KB_EXEC),)
$(KB_GEN)/stamp: sdk/kapi.json sdk/gen_kapi.py sdk/kapi_abi_x86.py sdk/kapi_target_ppc.py sdk/target_profile.py
	@mkdir -p $(KB_GEN)
	python3 -B sdk/gen_kapi.py --target ppc-powermac --out-dir $(KB_GEN)
	@touch $@
$(KB_OBJ): $(KB_GEN)/stamp
$(KB_OUT)/gen/kapi/kapi_generated.o: $(KB_GEN)/stamp $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk
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
$(KB_OUT)/kernel/console/console.o: KB_TU_FLAGS = -Wno-sign-compare
$(KB_OUT)/%.o: %.c $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk $(KB_MUT_FILE) $(KB_MUT_EXTRA_FILE) $(KB_MUT_THIRD_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) $(KB_TU_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/%.o: %.S $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk $(KB_MUT_FILE) $(KB_MUT_EXTRA_FILE) $(KB_MUT_THIRD_FILE)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/kernel.ld: arch/ppc/kernel.ld platform/powermac/platform_kboot_layout.h $(KB_OUT)/memmap_layout.h
	$(KB_CC) $(KB_FLAGS) -MF $@.d -E -P -x c $< -o $@
$(KB_OUT)/motd.txt:
	@mkdir -p $(dir $@)
	@printf 'OS32 PowerPC shell\n' > $@


# kboot_tick is the acceptance meter's name for the logical 100 Hz tick_count (physical DEC is 1 kHz).
# R-b keeps the machine reset provider ahead of the KAPI binding in R-c.
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

-include $(KB_OBJ:.o=.d)

.PHONY: kernel-link kernel-compile module modules sdk programs libs boot deploy deploy-kernel kernel-r1
kernel-link:
	@echo 'unsupported kernel link target ppc-powermac' >&2; exit 1
# The image is compiled only through the kernel target (its flags and output tree).
kernel-compile:
	@echo 'unsupported compile-only target ppc-powermac: use kernel' >&2; exit 1
module modules sdk programs libs boot deploy deploy-kernel kernel-r1 sqlite-module-proto kdebug hwdev:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1
%-module deploy-%:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1


include build/ppc-user.mk

# root の像に入るユーザーのバイナリ。この make の規則 (build/ppc-user.mk、置き場 PPC_USER_OUT) が像の置き場
# KB_SH_OUT に出すなら、そのまま組む。置き場が違う時 (link の像は置き場と profile が別) は、置き場と profile を
# 替えて同じ Makefile を呼ぶ (呼ばれた側は置き場が一致するので、そのまま組む)。
.PHONY: kb-user
ifeq ($(KB_SH_OUT),$(PPC_USER_OUT))
kb-user: $(ROOT_BINS)
else
kb-user:
	$(MAKE) ARCH=ppc PLATFORM=powermac PPC_USER_OUT=$(KB_SH_OUT)$(if $(filter 1,$(KBOOT_LINK)), PPC_SH_PROFILE=userland/ppc/sh/link_profile.h) $(ROOT_BINS)

$(ROOT_BINS): kb-user
endif
