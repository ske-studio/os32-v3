# PowerPC core-ready kernel (milestone 2, docs/decisions/ppc/PPC_KBOOT_DESIGN.md).
# Independent of the x86 graph: explicit source manifest = the closure reached
# from init/ppc_kernel.c plus the takeover/vector/CPU entry objects. No libc,
# no libgcc; undefined symbols are a build failure. x86 objects are untouched.
#   make -f build/ppc-kboot.mk            -> build/out/ppc-kboot/PPCKERN.ELF
#   make -f build/ppc-kboot.mk NEGATIVE=1 -> build/out/ppc-kboot/neg1/PPCKERN.ELF
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
KB_CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
KB_OBJCOPY := $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
KB_NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
KB_OBJDUMP := $(PPC_CROSS_DIR)/bin/powerpc-elf-objdump
NEGATIVE ?= 0
KBOOT_SHELL ?= 0
KBOOT_SH ?= 0
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
KB_ROOT_BINS ?= build/out/ppc-user/hello.bin build/out/ppc-user/fault.bin
KB_EXEC := $(filter 1,$(KBOOT_SHELL) $(KBOOT_SH))
KB_OUT ?= build/out/ppc-kboot$(if $(filter 1,$(KBOOT_SHELL)),/shell,$(if $(filter 1,$(KBOOT_SH)),/sh,$(if $(filter-out 0,$(NEGATIVE)),/neg$(NEGATIVE))))
ifeq ($(KB_FD1_STREAM)$(KBOOT_SH),10)
KB_OUT := $(KB_OUT)/fd1-stream
endif
KB_ELF := $(KB_OUT)/PPCKERN.ELF
.DEFAULT_GOAL := all

ROOT_ISO := $(KB_OUT)/root.iso
ROOT_BLOB := $(KB_OUT)/root_image_blob.o
ROOT_MOTD := $(KB_OUT)/motd.txt
ROOT_BINS = $(KB_ROOT_BINS) $(if $(filter 1,$(KBOOT_SH)),build/out/ppc-user/sh.bin)
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
KB_ASM := arch/ppc/kernel_entry.S arch/ppc/entry.S arch/ppc/kernel_takeover.S \
 arch/ppc/kernel_vectors.S arch/ppc/kernel_mmu.S arch/ppc/kernel_probe.S \
 arch/ppc/mmu.S arch/ppc/cache_copy.S
KB_ARCH := arch/ppc/kernel_boot.c arch/ppc/kernel_cpu.c arch/ppc/kernel_hpt.c \
 arch/ppc/kernel_trap.c arch/ppc/kernel_dec.c arch/ppc/pd_walk.c \
 arch/ppc/kboot_unavailable.c arch/ppc/dma_cache.c
KB_PLATFORM := platform/powermac/rh_observe.c platform/powermac/ofw.c platform/powermac/escc.c \
 platform/powermac/escc_tree.c platform/powermac/kboot_capture.c \
 platform/powermac/gmac_tree.c platform/powermac/gmac_clock.c \
 platform/powermac/kboot_reserve.c platform/powermac/memory.c \
 platform/powermac/mpic_tree.c platform/powermac/mpic.c \
 platform/powermac/clock.c platform/powermac/tick_budget.c platform/powermac/log.c \
 platform/powermac/kboot_console.c platform/powermac/pci_config.c \
 platform/powermac/gmac_dma.c platform/powermac/lan_gem_io.c \
 platform/powermac/lan_log.c platform/powermac/lan_wire.c \
 platform/powermac/lan_monitor.c platform/powermac/lan_diag.c \
 platform/powermac/lan_service.c platform/powermac/lan_terminal.c \
 platform/powermac/terminal_input.c
# Common kernel code, compiled unmodified.
KB_COMMON := init/ppc_kernel.c mm/memory_boot.c mm/paging.c mm/physmem.c \
 mm/pgalloc.c mm/kmalloc.c mm/shm.c kernel/irq/tick.c kernel/irq/tick_budget.c kernel/time/tick.c \
 lib/kprintf.c lib/kstring_c.c lib/divdi3.c kernel/sys.c kernel/idle_work.c fs/fd_redirect.c
KB_GEM := drivers/net/gem/gem_reset.c drivers/net/gem/gem_phy.c \
 drivers/net/gem/gem_ring.c drivers/net/gem/gem_mac.c \
 drivers/net/gem/gem_tx.c drivers/net/gem/gem_rx.c
KB_C := $(KB_ARCH) $(KB_PLATFORM) $(KB_COMMON) $(KB_GEM)
ifneq ($(KB_EXEC),)
ifneq ($(KBOOT_TERM),lan)
$(error KBOOT_TERM must be lan)
endif
KB_C += fs/vfs.c fs/vfs_fd.c drivers/base/dev.c $(RAM_ROOT_C) platform/powermac/root_boot.c platform/powermac/root_image.c \
 platform/powermac/console_stream.c platform/powermac/console_log.c
ifeq ($(KBOOT_SHELL),1)
KB_C += platform/powermac/cli_boot.c kernel/cli/resident_cli.c
else
KB_C += platform/powermac/shell_boot.c platform/powermac/input_boot.c \
 platform/powermac/input_decode.c
endif
KB_C += $(wildcard exec/*.c) $(wildcard arch/ppc/exec_*.c) \
 arch/ppc/syscall_code.c arch/ppc/kapi_call.c platform/powermac/exec_boot.c \
 kernel/input_service.c kernel/kbd_inject.c kernel/console/con_sink.c \
 fs/pipe_buffer.c kapi/kapi_strings.c kapi/kapi_print.c lib/kstring.c mm/paging_app.c \
 kernel/time/clock_service.c lib/os_time.c
KB_C := $(filter-out arch/ppc/kboot_unavailable.c platform/powermac/kboot_console.c,$(KB_C))
KB_C += kernel/console/console.c kernel/console/console_dual.c kernel/console/fbcon.c \
 kernel/console/font_source.c kernel/console/font_guard.c kernel/console/unicode_file.c \
 init/bootlog.c kernel/port_service.c kernel/serial_diag.c lib/utf8.c lib/kprintf_attr.c \
 platform/powermac/console.c platform/powermac/display.c \
 platform/powermac/display_provider.c platform/powermac/display_dual.c \
 drivers/video/kcg/kcg_file.c drivers/video/of_fb/backend_of_fb.c drivers/video/of_fb/of_fb_v2.c \
 platform/none/legacy_guest.c kernel/time/cpu_calibrate.c kernel/time/cpu_calibrate_math.c \
 gfx/gfx_core.c gfx/gfx_console.c gfx/gfx_policy.c gfx/palette.c gfx/gfx_scroll.c \
 $(filter-out gfx/v2/a0_weak.c gfx/sync/a0_weak.c,$(wildcard gfx/v2/*.c gfx/sync/*.c))
KB_ASM += arch/ppc/exec_resume.S arch/ppc/exec_enter.S arch/ppc/setjmp.S
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
ifeq ($(KBOOT_SH),1)
KB_C += platform/powermac/input_usb.c platform/powermac/input_keymap.c
KB_C += platform/powermac/usb_tree.c platform/powermac/usb_platform.c platform/powermac/usb_boot.c \
 $(wildcard drivers/usb/usb_*.c drivers/usb/ohci_*.c drivers/usb/hid_*.c) drivers/usb/ehci_quiesce.c
KB_C += platform/powermac/hostfs_io.c platform/powermac/hostfs_peer.c \
 platform/powermac/hostfs_boot.c fs/serialfs/serialfs.c \
 fs/serialfs/serialfs_persistent.c fs/serialfs/sfs_client.c \
 fs/serialfs/sfs_proto.c lib/crc32.c
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
 -D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=$(NEGATIVE) -DKBOOT_SHELL=$(KBOOT_SHELL) -DKBOOT_SH=$(KBOOT_SH) -MMD -MP $(KB_INC)

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
$(KB_OUT)/kernel/console/console.o: KB_TU_FLAGS = -Wno-sign-compare -DCONSOLE_PUTCHAR_MIRROR=1
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
	$(MAKE) ARCH=ppc PLATFORM=powermac CROSS_DIR=$(PPC_CROSS_DIR) $(ROOT_BINS)

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
	@python3 -B tools/ppc/kboot_audit.py --elf $(KB_ELF) --nm $(KB_NM) --objdump $(KB_OBJDUMP) \
	 --sources $(filter-out fs/isofs/iso9660.c,$(KB_C)) $(KB_ASM) $(if $(KB_MUT_SRC),--mutant $(KB_MUT_SRC) $(KB_MUT_FILE)) $(if $(KB_MUT_EXTRA_SRC),--mutant $(KB_MUT_EXTRA_SRC) $(KB_MUT_EXTRA_FILE)) $(if $(KB_MUT_THIRD_SRC),--mutant $(KB_MUT_THIRD_SRC) $(KB_MUT_THIRD_FILE)) \
	 -- $(KB_CC) $(KB_FLAGS)

-include $(KB_OBJ:.o=.d) $(if $(KB_EXEC),$(KB_OUT)/pm_log_printf.d)
