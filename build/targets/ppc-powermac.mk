# PowerPC (PowerMac) の製品の規則。最上位の Makefile が ARCH=ppc PLATFORM=powermac の時に読む。
#   make ARCH=ppc PLATFORM=powermac [IMAGE=kboot-*] [kernel]       -> build/out/ppc-kboot/<像>/PPCKERN.ELF
#   make ARCH=ppc PLATFORM=powermac KBOOT_SH=1 KBOOT_LINK=0 …      (道具は旗でも像を選べる)
#   make ARCH=ppc PLATFORM=powermac KBOOT_LINK=0 NEGATIVE=1        -> build/out/ppc-kboot/neg1/PPCKERN.ELF
# ソースは x86 と同じ所有者集合 (build/select.mk) から像の構成で選ぶ。像の選び方は
# sys/conf/images/ppc-flags.mk。libc・libgcc は使わず、未解決のシンボルはビルドの失敗。
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
KB_CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
KB_OBJCOPY := $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
KB_NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
KB_OBJDUMP := $(PPC_CROSS_DIR)/bin/powerpc-elf-objdump
KBOOT_TERM ?= lan
KB_ROOT_BINS ?=
KB_EXEC := $(filter 1,$(FEATURE_EXEC))
KB_C_LEGACY := -DKBOOT_SH=$(FEATURE_USERSHELL) -DKBOOT_LINK=$(FEATURE_LINK)
ifeq ($(IMAGE),powermac)
KB_OUT ?= build/out/ppc-powermac
else ifeq ($(origin KB_OUT),undefined)
KB_OUT := build/out/ppc-kboot$(if $(filter kboot-link kboot-link-sh,$(IMAGE)),/$(IMAGE:kboot-%=%),$(if $(filter-out 0,$(NEGATIVE)),/neg$(NEGATIVE)))
KB_SH_OUT ?= build/out/ppc-user$(if $(filter 1,$(FEATURE_LINK)),/link-sh)
endif
KB_SH_OUT ?= $(KB_OUT)/user
KB_ELF := $(KB_OUT)/PPCKERN.ELF

ROOT_ISO := $(KB_OUT)/root-input.iso
ROOT_BLOB :=
ROOT_ISO_FLAGS := --fixed-dates
ROOT_MOTD := $(KB_OUT)/motd.txt
# Only the C-GUI acceptance root boots the resident window manager.
ifeq ($(PPC_C_GUI),1)
$(KB_OUT)/sys/platform/powermac/shell_loop_ops.o: private KB_TU_FLAGS += -DOS32_C_GUI=1
KB_GUI_BINS ?= $(KB_SH_OUT)/gshell.bin
endif
ROOT_BINS = $(strip $(KB_ROOT_BINS) $(KB_GUI_BINS))
KB_INSTALLERS := $(if $(filter 1,$(FEATURE_IDE)),$(addprefix $(KB_SH_OUT)/,install.bin cdinst.bin))
KB_ROOT_SHELL = $(KB_SH_OUT)/shell.bin
ROOT_OBJCOPY := $(KB_OBJCOPY)
ROOT_ELF_FORMAT := elf32-powerpc
ROOT_CPU := powerpc
ROOT_MANIFEST ?= build/ppc-ram-root.json
PPC_UNICODE ?= build/out/unicode.bin
ROOT_DEPS ?= assets/fonts/shino16.kcgfont assets/fonts/JF-Dot-Shinonome16-LICENSE.txt $(PPC_UNICODE) $(KB_ROOT_SHELL)
KB_ROOT_DIAG := $(if $(filter 1,$(FEATURE_KDEBUG)),$(KB_OUT)/kdebug.bin)
# Set PPC_SYSTEM_CFG to a system.cfg containing KDEBUG=1 for a diagnostic root.
PPC_SYSTEM_CFG ?=
# Acceptance builds use a private resident binary (including source mutants).
KB_ROOT_FEP := $(if $(KB_EXEC),$(KB_OUT)/fep/fep.mod)
KB_ROOT_TEMPLATE := $(ROOT_MANIFEST)
ifneq ($(filter-out build/out/ppc-user/link-sh,$(KB_SH_OUT))$(KB_ROOT_DIAG)$(KB_ROOT_FEP)$(KB_INSTALLERS)$(PPC_SYSTEM_CFG),)
override ROOT_MANIFEST := $(KB_OUT)/root-manifest.json
$(ROOT_MANIFEST): $(KB_ROOT_TEMPLATE) $(PPC_SYSTEM_CFG) $(KB_ROOT_SHELL) $(KB_INSTALLERS) $(KB_ROOT_DIAG) $(KB_ROOT_FEP) $(KB_OUT)/root-shell.config build/targets/ppc-powermac.mk
	@mkdir -p $(@D)
	python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); d["/sys/shell.bin"]=sys.argv[2]; d.update({"/boot/kdebug.bin":sys.argv[3]} if sys.argv[3] else {}); d.update({"/etc/system.cfg":sys.argv[4]} if sys.argv[4] else {}); d.update({"/boot/fep.mod":sys.argv[5]} if sys.argv[5] else {}); d.update({"/sbin/"+p.rsplit("/",1)[-1]:p for p in sys.argv[6:]}); print(json.dumps(d))' "$<" "$(KB_ROOT_SHELL)" "$(KB_ROOT_DIAG)" "$(PPC_SYSTEM_CFG)" "$(KB_ROOT_FEP)" $(KB_INSTALLERS) > $@.tmp
	mv $@.tmp $@
endif

# RAW fixture: byte at offset i is i modulo 256 (320 sectors of 512 bytes).
PPC_LOOP_SAMPLE ?= build/out/ppc-loop/sample.img
$(PPC_LOOP_SAMPLE): build/targets/ppc-powermac.mk
	@mkdir -p $(@D)
	python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)) * 640)' > $@.tmp
	mv $@.tmp $@

include build/font-fetch.mk

$(PPC_UNICODE): tools/gen_unicode.c lib/unicode_jis_table.h sys/kern/console/unicode_file.h sys/include/endian_le.h sys/include/types.h sys/include/callconv.h
	@mkdir -p $(dir $@)
	gcc -std=gnu11 -O2 -I. -Iinclude -Isys/include -Isys/hal/time -Isys/hal/gfx -Isys/hal/input -Isys/hal/sound tools/gen_unicode.c -o $(dir $@)gen_unicode
	$(dir $@)gen_unicode --pairs $@

include build/ram-root.mk

# Rebuild when changing which application paths are installed.
$(ROOT_ISO): $(KB_ROOT_SHELL) $(KB_OUT)/root-shell.config
.PHONY: force-root-shell-config
$(KB_OUT)/root-shell.config: force-root-shell-config
	@mkdir -p $(@D)
	@printf '%s\n' '$(ROOT_MANIFEST) $(KB_ROOT_TEMPLATE) $(PPC_SYSTEM_CFG)' '$(ROOT_DEPS)' '$(KB_ROOT_SHELL) $(ROOT_BINS) $(KB_ROOT_DIAG)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# Entry objects first (kernel.ld keeps .text.entry at the image start).
KB_ASM := $(GAS_KERNEL)
KB_C := $(C_KERNEL)
ifeq ($(FEATURE_LINK),1)
KB_INC_EXTRA += -Inet -Isys/net -Isys/hal/net
endif
ifneq ($(KB_EXEC),)
ifneq ($(KBOOT_TERM),lan)
$(error KBOOT_TERM must be lan)
endif
ifneq ($(filter-out $(KB_C),$(RAM_ROOT_C)),)
$(error RAM root sources are not selected: $(filter-out $(KB_C),$(RAM_ROOT_C)))
endif
export SOURCE_DATE_EPOCH ?= 946684800
KB_EXTRA := $(KB_OUT)/gen/sys/kapi/kapi_generated.o $(KB_OUT)/build_id.o
KB_GEN := $(KB_OUT)/gen
KB_INC_EXTRA += -I$(KB_GEN)/sys/include -I$(KB_GEN)/sdk/include/os32 \
 -Ikapi -Isys/kapi -Idrivers -Inet -Isys/net -Isys/hal/net -Igfx -Isys/hal/gfx -Isys/kern/console -Isys/kern/gfx/osri -Iexec -Isys/kern/exec \
 -Idrivers/char -Isys/dev/char -Idrivers/net -Idrivers/dma -Isys/dev/dma -Idrivers/input -Isys/platform/pc98/input -Isys/dev/input -Idrivers/pci -Isys/dev/pci \
 -Idrivers/sound -Isys/hal/sound -Idrivers/video -Ifs/fat -Isys/fs/fat -Ifs/ext2 -Isys/fs/ext2 -Isys/platform/pc98/storage -Isys/dev/block -Ifs/ramfs -Isys/fs/ramfs \
 -Ifs/serialfs -Isys/fs/serialfs -Ikernel/module -Isys/kern/module -Ikernel/v86 -Isys/kern/v86 -Ikernel/ime -Isys/kern/ime \
 -Igfx/sync -Isys/hal/gfx/sync -Igfx/v2 -Isys/hal/gfx/v2 -Idrivers/video/geforce -Isys/dev/video/geforce -Isys/platform/pc98/video/geforce -Idrivers/video/kcg -Isys/dev/video/kcg
KB_INC_EXTRA += -Idrivers/ata -Isys/dev/ata -Ikernel/dev -Isys/kern/dev -Isys/hal/block -Isys/dev/ide -Isys/dev/kcg -Isys/dev/loop -Isys/dev/pci -Ifs -Isys/fs -Ifs/isofs -Isys/fs/isofs -Idrivers/block -Isys/dev/block -Ikernel/cli -Isys/kern/cli
endif
# Same machine flags as build/arch/ppc.mk:8 for every kernel TU (design section 4).
KB_MACHINE := -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none \
 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon -fsigned-char \
 -fno-short-enums
KB_INC := $(if $(KB_EXEC),-I$(KB_GEN)/sys/include -I$(KB_GEN)/sdk/include/os32) -I$(KB_OUT) -I. -Iinclude -Isys/include -Isys/hal/time -Isys/hal/gfx -Isys/hal/input -Isys/hal/sound -Iarch/ppc -Isys/arch/ppc -Iplatform/powermac -Isys/platform/powermac -Isdk/include \
 -Isdk/include/os32 -Ikernel -Isys/kern -Isys/hal/input -Iinit -Isys/kern/init -Ikernel/console -Isys/kern/console -Ikernel/irq -Isys/kern/irq -Ikernel/time -Isys/hal/time -Isys/platform/pc98/time -Isys/kern/time \
 -Imm -Isys/kern/mm -Ilib -Isys/kern $(KB_INC_EXTRA)
KB_FLAGS := -DPM_KDEBUG -DPM_LOAD_KDEBUG=$(FEATURE_KDEBUG) -DPM_ROOT_FRAME=$(if $(KB_EXEC),1,0) -std=gnu11 $(KB_MACHINE) -fno-builtin -fno-asynchronous-unwind-tables \
 -O2 -Wall -Wextra -Werror -Werror=vla -Werror=implicit-int \
 -Werror=implicit-function-declaration -ffunction-sections -fdata-sections \
 -D__cdecl= -D__KERNEL_BUILD__ $(HOOK_DEFS) -DOS32_FEATURE_IDE=$(FEATURE_IDE) -DOS32_FEATURE_LOOP=$(FEATURE_LOOP) -DOS32_FEATURE_USERSHELL=$(FEATURE_USERSHELL) -DKBOOT_NEGATIVE=$(NEGATIVE) $(KB_C_LEGACY) -MMD -MP $(KB_INC)

ifeq ($(FEATURE_SQLITE),1)
include build/sqlite-gen.mk
# Keep one dependency-tracked body for each compiler/flag configuration.
# Body mutations use the image's own output and source substitution rules.
KB_BODY_OUT := $(KB_OUT)
ifeq ($(filter $(SQLITE_HARD_C) sys/arch/ppc/sqlite_restore.S,$(KB_MUT_SRC) $(KB_MUT_EXTRA_SRC) $(KB_MUT_THIRD_SRC) $(KB_MUT_FOURTH_SRC) $(KB_MUT_FIFTH_SRC)),)
KB_BODY_KEY := $(shell printf '%s\n' '$(KB_CC) $(subst -I$(KB_OUT),-I@out,$(KB_FLAGS)) $(KB_TU_FLAGS)' | sha256sum | cut -d' ' -f1)
KB_BODY_OUT := build/out/ppc-kboot/sqlite-cache/$(KB_BODY_KEY)
endif
KB_VFS := $(KB_BODY_OUT)/gen/sqlite/os32_sqlite_vfs
KB_HARD := $(addprefix $(KB_BODY_OUT)/,$(SQLITE_HARD_C:.c=.o) sys/arch/ppc/sqlite_restore.o) $(KB_VFS).o
KB_GROUP := $(KB_BODY_OUT)/gen/sqlite/body
KB_EXTRA += $(KB_GROUP).o
KB_SQLITE_AUDIT := --sqlite-group $(KB_GROUP).o --sqlite-inputs $(KB_HARD)
KB_SQLITE_INC := -Ilib/sqlite3 -Ilib/softfloat
KB_KEEP_SQLITE := -Wl,--undefined=sqlite_module_init
$(KB_OUT)/sys/hal/input/legacy_ring.o $(KB_OUT)/sys/hal/input/legacy_keymap.o: private KB_TU_FLAGS += -DINPUT_COOKED_MODS=1
$(KB_OUT)/sys/kern/fep_boot.o $(KB_OUT)/sys/kern/sqlite_engine.o $(KB_OUT)/sys/kern/sqlite_outer.o $(KB_OUT)/sys/kern/sqlite_static.o $(KB_OUT)/sys/kapi/kapi_db.o: $(SQLITE_GENERATED)
$(KB_OUT)/sys/kern/sqlite_static.o: private KB_TU_FLAGS += -Ilib/sqlite3
$(KB_OUT)/sys/kern/sqlite_outer.o: private KB_TU_FLAGS += -Ilib/sqlite3 -DOS32_SQLITE_STATIC
# The bundled port hard-codes LITTLEENDIAN in three headers. Select the
# upstream big-endian branches in a PPC-only quote include directory.
KB_SF_INC := $(KB_OUT)/gen/softfloat
KB_SF_HEADERS := $(addprefix $(KB_SF_INC)/,platform.h primitiveTypes.h softfloat_types.h)
$(KB_SF_INC)/%.h: lib/softfloat/%.h
	@mkdir -p $(@D)
	sed 's/^#define LITTLEENDIAN 1$$/#undef LITTLEENDIAN/' $< > $@
KB_SF_OBJS := $(addprefix $(KB_OUT)/,$(SOFTFLOAT_RESIDENT_SOURCES:.c=.o) lib/softfloat_bits.o)
ifeq ($(KB_BODY_OUT),$(KB_OUT))
KB_SF_OBJS += $(addprefix $(KB_OUT)/,$(SOFTFLOAT_CONV_SOURCES:.c=.o) lib/softfloat_abi.o)
endif
$(KB_SF_OBJS): $(KB_SF_HEADERS)
$(KB_SF_OBJS): private KB_QUOTE_FLAGS := -I- -iquote $(KB_SF_INC) -iquote lib/softfloat

ifneq ($(KB_BODY_OUT),$(KB_OUT))
.PHONY: ppc-sqlite-refresh
$(KB_GROUP).o: ppc-sqlite-refresh $(SQLITE_GENERATED)
	@mkdir -p $(KB_BODY_OUT)
	+@flock $(KB_BODY_OUT)/build.lock $(MAKE) KB_OUT=$(KB_BODY_OUT) KB_BODY_OUT=$(KB_BODY_OUT) KB_MUT_SRC= KB_MUT_FILE= KB_MUT_EXTRA_SRC= KB_MUT_EXTRA_FILE= KB_MUT_THIRD_SRC= KB_MUT_THIRD_FILE= KB_MUT_FOURTH_SRC= KB_MUT_FOURTH_FILE= KB_MUT_FIFTH_SRC= KB_MUT_FIFTH_FILE= $@
else
$(KB_HARD): $(SQLITE_GENERATED)
$(KB_HARD) $(KB_GROUP).raw.o: sys/conf/features/sqlite-static.mk build/softfloat.mk
$(KB_HARD): private KB_TU_FLAGS += -mhard-float -Os -Ilib/sqlite3 -Ilib/softfloat -Ifs -Isys/fs
$(KB_OUT)/lib/softfloat/s_roundPackToF64.o: private KB_TU_FLAGS += -Wno-unused-label
$(KB_VFS).o $(KB_OUT)/lib/sqlite3/sqlite3.o: private KB_TU_FLAGS += -std=gnu89 -DNDEBUG -Wno-long-long -include lib/sqlite3/os32_sqlite_config.h
$(KB_OUT)/lib/sqlite3/sqlite3.o: private KB_TU_FLAGS += -w
$(KB_VFS).c: tools/gen_sqlite_imports.py lib/sqlite3/os32_sqlite_vfs.c
	@mkdir -p $(@D)
	python3 -B $< --static-vfs $@
$(KB_VFS).o: $(KB_VFS).c
	$(KB_CC) $(KB_FLAGS) $(KB_TU_FLAGS) -c $(KB_VFS).c -o $@
$(KB_GROUP).raw.o: $(KB_HARD)
	$(PPC_CROSS_DIR)/bin/powerpc-elf-ld -r -d --fatal-warnings --gc-sections -u sqlite_module_init -Map=$@.map $(KB_HARD) -o $@
$(KB_GROUP).o: $(KB_GROUP).raw.o tools/ppc/sqlite_attributes.py
	$(KB_OBJCOPY) --dump-section .gnu.attributes=$(KB_GROUP).attr $< $@.tmp
	python3 -B tools/ppc/sqlite_attributes.py $(KB_GROUP).attr $(KB_GROUP).neutral
	$(KB_OBJCOPY) --keep-global-symbol=sqlite_module_init --update-section .gnu.attributes=$(KB_GROUP).neutral $< $@
	@rm -f $@.tmp
-include $(KB_HARD:.o=.d)
endif
endif

KB_OBJ := $(addprefix $(KB_OUT)/,$(KB_ASM:.S=.o) $(KB_C:.c=.o))
# The source lists and link order live in these files: a change there relinks the image.
KB_SELECT_MK := build/select.mk build/sources/common.mk build/sources/order.mk build/sources/hooks.mk \
 build/arch/ppc-sources.mk build/platform/powermac.mk sys/conf/images/$(IMAGE).mk
KB_SELECT_MK += $(PPC_FLAGS_MK) build/targets/ppc-powermac.mk
ifeq ($(IMAGE),kboot-link-sh)
KB_SELECT_MK += sys/conf/images/powermac.mk
endif
$(KB_OBJ) $(KB_ELF): $(KB_SELECT_MK)

.PHONY: all kernel audit
all: kernel
BOOT_ISO := $(KB_OUT)/boot.iso
ifeq ($(IMAGE),powermac)
include build/ppc-install-boot.mk
all: iso
endif
.PHONY: iso deploy-iso
iso: audit $(BOOT_ISO)
$(BOOT_ISO): $(KB_ELF) tools/ppc/boot_media.py build/targets/ppc-powermac.mk | audit
	python3 -B tools/ppc/boot_media.py "$(KB_ELF)" "$@" $(HD_MEDIA_ARGS)
deploy-iso: iso
	@test -n "$(ISO_DEST)" && test ! -d "$(ISO_DEST)"
	@set -e; tmp=$$(mktemp "$(ISO_DEST).XXXXXX"); trap 'rm -f "$$tmp"' EXIT; cp "$(BOOT_ISO)" "$$tmp"; mv -f "$$tmp" "$(ISO_DEST)"
kernel: $(KB_ELF) $(if $(KB_EXEC),$(KB_OUT)/root.iso) audit
ifneq ($(KB_EXEC),)
.PHONY: ppc-source-date-force
$(KB_OUT)/config/source-date.flags: ppc-source-date-force
	@mkdir -p $(@D)
	@printf '%s\n' '$(KB_CC) $(KB_FLAGS) SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
# The exec cleanup hook changes when SQLite is toggled in the same output tree.
$(KB_OUT)/sys/platform/powermac/root_boot.o $(KB_OUT)/sys/platform/powermac/ppc_kernel.o $(KB_OUT)/sys/kern/exec/exec.o $(KB_OUT)/sys/kapi/kapi_sysinfo.o: $(KB_OUT)/config/source-date.flags
endif

# 共通の置き場のソースは、PowerPC の像にまだ入っていない物 (C_COMMON_PC98_PENDING) も PowerPC の
# コンパイラを通す。機種の名前 (PC-98 の定数など) を間接に使っていれば、ここで未宣言になって止まる。
# リンクはしない (像には入れない)。
FEP_OUT := $(KB_OUT)/gen/fep
include build/fep-common.mk
$(KB_OUT)/sys/kern/ime/fep_module.o: $(FEP_GEN)
$(KB_OUT)/sys/kern/ime/fep_module.o: private KB_TU_FLAGS += -I$(FEP_OUT)
KB_PENDING_OBJ := $(addprefix $(KB_OUT)/,$(patsubst %.c,%.o,$(filter %.c,$(C_COMMON_PC98_PENDING) $(C_COMMON_COMPILE_ONLY_PPC))))
$(KB_OUT)/sys/kern/fep_boot.o $(KB_OUT)/sys/kern/sqlite_engine.o: KB_TU_FLAGS += -Ilib/sqlite3 -Ifs -Isys/fs
$(KB_OUT)/sys/kapi/kapi_db.o: KB_TU_FLAGS += -Ilib/sqlite3
.PHONY: common-compile
common-compile: $(KB_PENDING_OBJ)

$(KB_OUT)/memmap_layout.h: sys/include/memmap.h tools/gen_memmap.py
	@mkdir -p $(dir $@)
	python3 -B tools/gen_memmap.py --layout-header $@
ifneq ($(KB_EXEC),)
BUILD_ID_SRC := $(KB_OUT)/build_id.c
include build/build-id.mk
# Acceptance fixtures supply a complete generated binding tree.
ifneq ($(wildcard $(KB_GEN)/mutant.stamp),)
$(KB_GEN)/stamp: $(KB_GEN)/mutant.stamp
	@touch $@
else
$(KB_GEN)/stamp: $(KB_OUT)/config/source-date.flags sys/conf/images/$(IMAGE).mk sdk/kapi.json sdk/gen_kapi.py sdk/kapi_abi_x86.py sdk/kapi_target_ppc.py sdk/target_profile.py
	@mkdir -p $(KB_GEN)
	python3 -B sdk/gen_kapi.py --target ppc-powermac --feature-link $(FEATURE_LINK) --feature-ide $(FEATURE_IDE) --feature-fep $(FEATURE_FEP) --out-dir $(KB_GEN)
	@touch $@
endif
$(KB_OBJ): $(KB_GEN)/stamp
$(KB_OUT)/build_id.o: $(BUILD_ID_SRC)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $< -o $@
$(KB_OUT)/gen/sys/kapi/kapi_generated.o: $(KB_GEN)/stamp $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk build/sources/hooks.mk
	$(KB_CC) $(KB_FLAGS) -c $(KB_GEN)/sys/kapi/kapi_generated.c -o $@
-include $(KB_OUT)/build_id.d
-include $(KB_OUT)/gen/sys/kapi/kapi_generated.d
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
KB_MUT_FOURTH_SRC ?=
KB_MUT_FOURTH_FILE ?=
KB_MUT_FIFTH_SRC ?=
KB_MUT_FIFTH_FILE ?=
kb_slots := KB_MUT KB_MUT_EXTRA KB_MUT_THIRD KB_MUT_FOURTH KB_MUT_FIFTH
kb_files = $(foreach slot,$(kb_slots),$($(slot)_FILE))
kb_replacement = $(strip $(foreach slot,$(kb_slots),$(if $(filter $1,$($(slot)_SRC)),$($(slot)_FILE))))
kb_src = $(if $(call kb_replacement,$1),$(call kb_replacement,$1) -I$(dir $1),$1)
.PHONY: ppc-layout-force
$(KB_OUT)/config/c-gui.flags: ppc-layout-force
	@mkdir -p $(@D)
	@echo '$(filter 1,$(PPC_C_GUI))' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(KB_OUT)/sys/platform/powermac/shell_loop_ops.o: $(KB_OUT)/config/c-gui.flags
$(KB_OUT)/config/shell-layout.flags: ppc-layout-force
	@mkdir -p $(@D)
	@echo '$(filter 1,$(KBOOT_SHELL_LAYOUT_DIAG))' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(KB_OUT)/sys/platform/powermac/ppc_kernel.o: $(KB_OUT)/config/shell-layout.flags
ifeq ($(KBOOT_SHELL_LAYOUT_DIAG),1)
$(KB_OUT)/sys/platform/powermac/ppc_kernel.o: private KB_TU_FLAGS += -DKBOOT_SHELL_LAYOUT_DIAG=1
endif
$(KB_OUT)/config/usb-key-trace.flags: ppc-layout-force
	@mkdir -p $(@D)
	@echo '$(filter 1,$(KBOOT_USB_KEY_TRACE))' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(KB_OUT)/sys/platform/powermac/usb_key_report.o $(KB_OUT)/sys/platform/powermac/input_boot.o: $(KB_OUT)/config/usb-key-trace.flags
ifeq ($(KBOOT_USB_KEY_TRACE),1)
$(KB_OUT)/sys/platform/powermac/usb_key_report.o $(KB_OUT)/sys/platform/powermac/input_boot.o: private KB_TU_FLAGS += -DKBOOT_USB_KEY_TRACE=1
endif
$(KB_OUT)/config/gem-rx-diag.flags: ppc-layout-force
	@mkdir -p $(@D)
	@echo '$(filter 1,$(KBOOT_GEM_RX_DIAG))' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi
$(KB_OUT)/sys/platform/powermac/nic_gem.o $(KB_OUT)/sys/platform/powermac/lan_gem_io.o: $(KB_OUT)/config/gem-rx-diag.flags
ifeq ($(KBOOT_GEM_RX_DIAG),1)
$(KB_OUT)/sys/platform/powermac/nic_gem.o $(KB_OUT)/sys/platform/powermac/lan_gem_io.o: private KB_TU_FLAGS += -DKBOOT_GEM_RX_DIAG=1
endif
ifeq ($(IMAGE),powermac)
$(KB_OUT)/sys/platform/powermac/ide_tree.o: private KB_TU_FLAGS += -DPM_INSTALL_MEDIA=1
endif

$(KB_OUT)/sys/dev/ata/ide.o: private KB_TU_FLAGS += -Wno-sign-compare
$(KB_OUT)/lib/lz4.o: KB_TU_FLAGS += -O0
$(KB_OUT)/sys/kern/console/console.o: KB_TU_FLAGS = -Wno-sign-compare
$(KB_OUT)/%.o: %.c $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk build/sources/hooks.mk $(kb_files)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_QUOTE_FLAGS) $(KB_FLAGS) $(KB_TU_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/%.o: %.S $(KB_OUT)/memmap_layout.h build/targets/ppc-powermac.mk build/sources/hooks.mk $(kb_files)
	@mkdir -p $(dir $@)
	$(KB_CC) $(KB_FLAGS) -c $(call kb_src,$<) -o $@
$(KB_OUT)/kernel.ld: sys/arch/ppc/kernel.ld sys/platform/powermac/platform_kboot_layout.h $(KB_OUT)/memmap_layout.h
	$(KB_CC) $(KB_FLAGS) -MF $@.d -E -P -x c $< -o $@
$(KB_OUT)/motd.txt:
	@mkdir -p $(dir $@)
	@printf 'OS32 PowerPC shell\n' > $@


# kboot_tick is the acceptance meter's name for the logical 100 Hz tick_count (physical DEC is 1 kHz).
# Images without KAPI (KB_EXEC empty) have no sys_reboot caller; keep the shared reset provider (PPC_RESET_DESIGN §5).
KB_KEEP_RESET := $(if $(KB_EXEC),,-Wl,--undefined=platform_reset)
ifneq ($(KB_EXEC),)
KB_KEEP_PAIR := -Wl,--undefined=platform_module_pair
KB_PAIR_TOOL := tools/ppc/root_pair.py
KB_PAIR_OUTPUT := $(KB_OUT)/root.iso
include build/ppc-fep-module.mk
include build/ppc-root-pair.mk
KB_LINK_PREFIX = python3 -B $(KB_PAIR_TOOL) $(ROOT_ISO) $(KB_ELF) --
else
$(KB_ELF): $(KB_OBJ) $(KB_EXTRA) $(KB_OUT)/kernel.ld
	$(KB_LINK_COMMAND)
endif
KB_LINK_COMMAND = $(KB_LINK_PREFIX) $(KB_CC) $(KB_FLAGS) -MF $(KB_ELF).d -no-pie -Wl,--gc-sections -Wl,--no-undefined $(KB_KEEP_RESET) $(KB_KEEP_SQLITE) $(KB_KEEP_PAIR) \
	 -Wl,-z,noexecstack -Wl,--defsym,kboot_tick=tick_count -Wl,-T,$(KB_OUT)/kernel.ld \
	 -Wl,-Map,$(KB_ELF:.ELF=.map) $(KB_OBJ) $(KB_EXTRA) -o $(KB_ELF)

# H3 audits (tools/ppc/kboot_audit.py): nothing unresolved, integer-only code by
# decoded mnemonic, and no direct or macro-mediated user of the 100 Hz PIT_HZ
# inside the PPC closure: common code uses OS32_TICK_HZ (design R7). Mutated sources are audited too.
# ISO media tick-rate debt is documented in PPC_SHELL_DESIGN §8; RAM media is fixed.
audit: $(KB_ELF)
	@python3 -B tools/ppc/kboot_audit.py --elf $(KB_ELF) --map $(KB_ELF:.ELF=.map) --objects $(KB_OBJ) $(KB_EXTRA) $(KB_SQLITE_AUDIT) --fp-object $(KB_OUT)/sys/arch/ppc/fp_context.o --nm $(KB_NM) --objdump $(KB_OBJDUMP) \
	 --sources $(filter-out sys/fs/isofs/iso9660.c,$(KB_C)) $(KB_ASM) $(SQLITE_HARD_C) $(if $(KB_VFS),$(KB_VFS).c sys/arch/ppc/sqlite_restore.S) $(foreach slot,$(kb_slots),$(if $($(slot)_SRC),--mutant $($(slot)_SRC) $($(slot)_FILE))) \
	 -- $(KB_CC) $(KB_FLAGS) $(KB_SQLITE_INC) $(if $(filter 1,$(FEATURE_FEP)),-I$(FEP_OUT))

-include $(KB_OBJ:.o=.d)

.PHONY: kernel-link kernel-compile module modules sdk programs libs boot deploy deploy-kernel kernel-r1
kernel-link:
	@echo 'unsupported kernel link target ppc-powermac' >&2; exit 1
# The image is compiled only through the kernel target (its flags and output tree).
kernel-compile:
	@echo 'unsupported compile-only target ppc-powermac: use kernel' >&2; exit 1
module modules sdk programs libs boot deploy deploy-kernel kernel-r1 sqlite-module-proto hwdev:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1
.PHONY: kdebug
kdebug: $(KB_OUT)/kdebug.bin

$(KB_OUT)/diag.o: sys/platform/powermac/module_diag.c
	@mkdir -p $(@D)
	$(KB_CC) $(KB_FLAGS) -Ikernel/module -Isys/kern/module -c $< -o $@
$(KB_OUT)/kdebug.elf: $(KB_OUT)/diag.o $(KB_OUT)/sys/kern/kdebug/hwb_access.o $(KB_OUT)/sys/dev/pci/pci_hwb.o $(KB_OUT)/sys/kern/kstring_c.o build/ppc-module.ld sdk/link/generations.ld
	$(PPC_CROSS_DIR)/bin/powerpc-elf-ld --emit-relocs --gc-sections -u kdebug_ops -e kdebug_init -T build/ppc-module.ld $(filter %.o,$^) -o $@
KB_DIAG_METADATA ?= sys/platform/powermac/kdebug_imports.json
$(KB_OUT)/kdebug.bin: $(KB_OUT)/kdebug.elf $(KB_DIAG_METADATA) tools/mkmod.py tools/ppc/module_rela.py tools/module_schema.py sdk/kapi.json sdk/include/os32/os32_generations.h
	python3 -B tools/mkmod.py --target ppc-powermac --elf $< --metadata $(KB_DIAG_METADATA) --kapi-abi-gen $(shell python3 -c 'import json; print(json.load(open("sdk/kapi.json"))["version"])') --layout-gen $(shell sed -n 's/^\#define OS32_MEMORY_LAYOUT_GENERATION \([0-9]*\).*/\1/p' sdk/include/os32/os32_generations.h) -o $@
-include $(KB_OUT)/diag.d $(KB_OUT)/kdebug/hwb_access.d $(KB_OUT)/drivers/pci/pci_hwb.d

%-module deploy-%:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1


include build/ppc-user.mk

# root の像に入るユーザーのバイナリ。この make の規則 (build/ppc-user.mk、置き場 PPC_USER_OUT) が像の置き場
# KB_SH_OUT に出すなら、そのまま組む。置き場が違う時 (link の像は置き場と profile が別) は、置き場と profile を
# 替えて同じ Makefile を呼ぶ (呼ばれた側は置き場が一致するので、そのまま組む)。
.PHONY: kb-user
ifeq ($(KB_SH_OUT),$(PPC_USER_OUT))
kb-user: $(ROOT_BINS) $(KB_ROOT_SHELL) $(KB_INSTALLERS)
else
kb-user:
	$(MAKE) ARCH=ppc PLATFORM=powermac IMAGE=$(IMAGE) PPC_USER_OUT=$(KB_SH_OUT) $(ROOT_BINS) $(KB_ROOT_SHELL) $(KB_INSTALLERS)

$(ROOT_BINS) $(KB_ROOT_SHELL) $(KB_INSTALLERS): kb-user
endif
