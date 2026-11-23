# USER-only PowerPC applications use the selected SDK CRT and linker script.
ifeq ($(SDK_TARGET_CALL_ABI),powerpc-sysv)
PPC_USER_OUT ?= build/out/ppc-user
PPC_USER_CRT := $(addprefix $(PPC_USER_OUT)/,$(notdir $(SDK_TARGET_CRT_USER)))
PPC_USER_FLAGS := $(filter-out -mcpu=7450 -msoft-float,$(USER_CFLAGS)) $(PPC_USER_ABI) -fno-common -I sdk/include -I sdk/include/os32 -Wno-attributes

# All USER links share one validated CRT/allocator closure.
PPC_USER_CRT += $(PPC_USER_OUT)/syscalls.o $(PPC_USER_OUT)/help.o
PPC_USER_NANO := $(PPC_USER_OUT)/nano/libos32nano.a
# Recursive: parse must not run the compiler (unsupported targets reject with zero calls, test_sep5_boot).
PPC_USER_LIBC = $(shell $(CC) $(PPC_USER_ABI) -print-file-name=libc.a)
PPC_USER_LIBGCC = $(shell $(CC) $(PPC_USER_ABI) -print-libgcc-file-name)
PPC_USER_LIBS = -L$(dir $(PPC_USER_LIBC)) -L$(dir $(PPC_USER_LIBGCC)) $(PPC_USER_NANO) -lc -lgcc
PPC_USER_FLOAT ?= -u _printf_float
PPC_GUARDED_LD = python3 sdk/link_guard.py --target $(SDK_TARGET) --ar $(LD:%-ld=%-ar) $(LD)
PPC_USER_LINK = $(PPC_GUARDED_LD) -m $(SDK_TARGET_ELF_EMULATION) --gc-sections -z noexecstack -T $(SDK_TARGET_APP_LINK) $(PPC_USER_FLOAT)

$(PPC_USER_OUT)/syscalls.o: sdk/crt/syscalls.c sdk/allocator/nano_adapter.h userland/lib/rt/ls.c $(SDK_KAPI_HDR) | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(PPC_USER_OUT)/help.o: sdk/crt/help.c sdk/include/os32/help.h $(SDK_KAPI_HDR) | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(PPC_USER_NANO) $(PPC_USER_NANO:.a=.json) &: sdk/allocator/build_nano_target.py sdk/allocator/nano_adapter.c sdk/allocator/nano_adapter.h sdk/allocator/nano_inputs.json tools/check_nano_inputs_target.py tools/check_nano_inputs.py $(PPC_USER_OUT)/syscalls.o
	python3 -B sdk/allocator/build_nano_target.py --cross-dir $(PPC_CROSS_DIR) $(@D) --crt $(PPC_USER_OUT)/syscalls.o

PPC_USER_APPS := $(addprefix $(PPC_USER_OUT)/,hello fault nest outprobe nlprobe celltest shmprobe infoprob pciprob pathprob dbprob kcgprob bootprob fontprob surfprob sysiprob verprob portprob rtcprob loopprb gfx2prob dispprb in2prob mousprob sfsprob v89probe)

.PHONY: hello ppc-fault ppc-nest
hello: $(PPC_USER_OUT)/hello.bin
ppc-fault: $(PPC_USER_OUT)/fault.bin
ppc-nest: $(PPC_USER_OUT)/nest.bin

$(PPC_USER_OUT):
	mkdir -p $@

# Assembly has no C unit stamp; emit the same generation note alongside it.
$(PPC_USER_OUT)/crt0.o: sdk/crt/ppc/crt0.S sdk/os32x_hdr.py sdk/os32_generations.py | $(PPC_USER_OUT)
	python3 -B -c 'import sys; sys.path.insert(0,"sdk"); import os32x_hdr as h; print(".include \"sdk/crt/ppc/crt0.S\"\n.section .os32_generations,\"\",@progbits\n.long %d,%d,%d,%d" % (h.OS32X_HDR_VERSION,h.OS32_KAPI_ABI_GENERATION,h.OS32_MEMORY_LAYOUT_GENERATION,h.OS32_SHLIB_PROTOCOL))' > $(PPC_USER_OUT)/crt0-stamped.S
	$(CC) $(PPC_USER_MACHINE) -c $(PPC_USER_OUT)/crt0-stamped.S -o $@

PPC_USER_START_SRC ?= sdk/crt/ppc/start.c
$(PPC_USER_OUT)/crt0_c.o: $(PPC_USER_START_SRC) $(SDK_KAPI_HDR) sdk/include/os32/os32_kapi_generated.h sdk/include/os32/os32_unit_stamp.h | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(PPC_USER_OUT)/pathprob.o: lib/path.h
$(PPC_USER_OUT)/pciprob.o: include/pci_dev_contract.h include/pci_bind_contract.h
$(PPC_USER_OUT)/dispprb.o: include/app_ids.h
$(PPC_USER_OUT)/fontprob.o: include/config.h

$(addsuffix .o,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.o: userland/ppc/%.c | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

PPC_GUIPROB := $(PPC_USER_OUT)/$(if $(filter 1,$(PPC_C_GUI)),c-gui/)guiprob
$(PPC_GUIPROB).o: userland/ppc/guiprob.c build/ppc-user.mk | $(PPC_USER_OUT)
	@mkdir -p $(@D)
	$(CC) $(PPC_USER_FLAGS) $(if $(filter 1,$(PPC_C_GUI)),-DC_GUI_ACCEPT) -c $< -o $@

$(PPC_USER_OUT)/memstat.o: userland/tests/mem_stat_test.c $(SDK_KAPI_HDR) | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(PPC_USER_OUT)/atadeny.o: tools/ppc/ata_deny.c $(SDK_KAPI_HDR) | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(PPC_USER_OUT)/learnprb.o: tools/ppc/learn_probe.c $(SDK_KAPI_HDR) | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

PPC_USER_APPS += $(PPC_GUIPROB) $(PPC_USER_OUT)/memstat $(PPC_USER_OUT)/atadeny $(PPC_USER_OUT)/learnprb
$(addsuffix .elf,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.elf: $(PPC_USER_CRT) $(PPC_USER_OUT)/%.o $(SDK_TARGET_APP_LINK) sdk/link_guard.py sdk/allocator/check_link_target.py $(PPC_USER_NANO) $(PPC_USER_NANO:.a=.json)
	$(PPC_USER_LINK) -o $@ $(filter %.o,$^) $(PPC_USER_LIBS)

$(addsuffix .raw,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.raw: $(PPC_USER_OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(addsuffix .bin,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.bin: $(PPC_USER_OUT)/%.raw $(PPC_USER_OUT)/%.elf sdk/mkos32x.py sdk/os32x_hdr.py sdk/os32_generations.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/$*.elf

PPC_INST_DIR := $(PPC_USER_OUT)/installer
PPC_INST_SRC := platform/powermac/install_target.c userland/system/inst_hdd.c userland/system/inst_disk.c userland/shell/hdprep_plan.c fs/ext2/ext2_layout.c
PPC_INST_COMMON := $(addprefix $(PPC_INST_DIR)/,$(PPC_INST_SRC:.c=.o))
PPC_INST_EXTRA := userland/system/install.c userland/system/cdinst.c userland/lib/rt/pkg.c userland/lib/rt/dbgserial.c
$(PPC_INST_DIR)/userland/system/install.o $(PPC_INST_DIR)/userland/lib/rt/pkg.o: private PPC_USER_FLAGS += -DOS32_VERIFY_INSTALLED_FILES
$(PPC_INST_COMMON) $(addprefix $(PPC_INST_DIR)/,$(PPC_INST_EXTRA:.c=.o)): $(PPC_INST_DIR)/%.o: %.c build/ppc-user.mk $(PPC_INST_MUT_FILE)
	@mkdir -p $(@D)
	$(CC) $(PPC_USER_FLAGS) -DOS32_FEATURE_IDE=1 -Iplatform/powermac -I. -Iinclude -Iuserland/lib -Iuserland/lib/rt -Iuserland/system -c $(if $(filter $<,$(PPC_INST_MUT_SRC)),$(PPC_INST_MUT_FILE),$<) -o $@

$(PPC_USER_OUT)/install.elf: $(PPC_INST_DIR)/userland/system/install.o
$(PPC_USER_OUT)/cdinst.elf: $(PPC_INST_DIR)/userland/system/cdinst.o $(PPC_INST_DIR)/userland/lib/rt/pkg.o $(PPC_INST_DIR)/userland/lib/rt/dbgserial.o
$(addprefix $(PPC_USER_OUT)/,install.elf cdinst.elf): $(PPC_USER_CRT) $(PPC_INST_COMMON) $(SDK_TARGET_APP_LINK) $(PPC_USER_NANO)
	$(PPC_USER_LINK) -o $@ $(filter %.o,$^) $(PPC_USER_LIBS)

$(addprefix $(PPC_USER_OUT)/,install.raw cdinst.raw): $(PPC_USER_OUT)/%.raw: $(PPC_USER_OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(addprefix $(PPC_USER_OUT)/,install.bin cdinst.bin): $(PPC_USER_OUT)/%.bin: $(PPC_USER_OUT)/%.raw $(PPC_USER_OUT)/%.elf
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/$*.elf

-include $(wildcard $(PPC_INST_DIR)/*/*/*.d $(PPC_INST_DIR)/*/*/*/*.d)

# ISO 9660 level 1 root: bin names must be 8.3 (tools/mkroot_image.py). Acceptance-only target: no addition to PPC_USER_APPS or default ROOT_BINS.
$(PPC_USER_OUT)/fp_probe.o: userland/ppc/fp_probe_main.c | $(PPC_USER_OUT)
	$(CC) $(filter-out -mhard-float,$(PPC_USER_FLAGS)) -msoft-float -c $< -o $@

$(PPC_USER_OUT)/fp_probe_asm.o: userland/ppc/fp_probe.S sdk/os32x_hdr.py sdk/os32_generations.py | $(PPC_USER_OUT)
	python3 -B -c 'import sys; sys.path.insert(0,"sdk"); import os32x_hdr as h; print(".include \"userland/ppc/fp_probe.S\"\n.section .os32_generations,\"\",@progbits\n.long %d,%d,%d,%d" % (h.OS32X_HDR_VERSION,h.OS32_KAPI_ABI_GENERATION,h.OS32_MEMORY_LAYOUT_GENERATION,h.OS32_SHLIB_PROTOCOL))' > $(PPC_USER_OUT)/fp-probe-stamped.S
	$(CC) $(PPC_USER_MACHINE) -c $(PPC_USER_OUT)/fp-probe-stamped.S -o $@

$(PPC_USER_OUT)/fp_probe.elf: $(PPC_USER_CRT) $(PPC_USER_OUT)/fp_probe.o $(PPC_USER_OUT)/fp_probe_asm.o $(SDK_TARGET_APP_LINK) sdk/link_guard.py sdk/allocator/check_link_target.py $(PPC_USER_NANO) $(PPC_USER_NANO:.a=.json)
	$(PPC_USER_LINK) -o $@ $(filter %.o,$^) $(PPC_USER_LIBS)

$(PPC_USER_OUT)/fp_probe.raw: $(PPC_USER_OUT)/fp_probe.elf
	$(OBJCOPY) -O binary $< $@

$(PPC_USER_OUT)/fp_probe.bin: $(PPC_USER_OUT)/fp_probe.raw $(PPC_USER_OUT)/fp_probe.elf sdk/mkos32x.py sdk/os32x_hdr.py sdk/os32_generations.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/fp_probe.elf

# The resident shell uses the fixed break backend and the complete command set.
PPC_RES_DIR := $(PPC_USER_OUT)/resident
PPC_RES_SRC := $(wildcard userland/shell/*.c userland/lib/save/*.c) \
 drivers/pci/pci_decode.c fs/pc98pt.c userland/lib/filer/filer_draw.c \
 userland/lib/filer/filer_core.c sdk/crt/syscalls.c sdk/allocator/nano_adapter.c
PPC_RES_SRC += $(if $(filter 1,$(FEATURE_IDE)),platform/powermac/install_target.c)
PPC_RES_OBJ := $(addprefix $(PPC_RES_DIR)/,$(PPC_RES_SRC:.c=.o))
$(PPC_RES_OBJ): $(PPC_RES_DIR)/%.o: %.c build/ppc-user.mk $(SDK_KAPI_HDR) $(PPC_SH_MUT_FILE) $(PPC_RES_MUT_FILE)
	@mkdir -p $(@D)
	$(CC) $(PPC_USER_FLAGS) -DOS32_CRT_RESIDENT -DOS32_FEATURE_IDE=$(FEATURE_IDE) -Iplatform/powermac -I. -Iinclude -Iuserland/lib -Iuserland/shell -Iuserland/lib/filer -Iuserland/lib/gfx -Iuserland/lib/save -Idrivers -c $(if $(filter $<,$(PPC_RES_MUT_SRC)),$(PPC_RES_MUT_FILE),$(if $(filter $<,$(PPC_SH_MUT_SRC)),$(PPC_SH_MUT_FILE),$<)) -o $@

$(PPC_RES_DIR)/memmap_layout.h: include/memmap.h tools/gen_memmap.py
	@mkdir -p $(@D)
	python3 -B tools/gen_memmap.py --layout-header $@

$(PPC_RES_DIR)/app_sys.ld: sdk/link/ppc/app_sys.ld $(PPC_RES_DIR)/memmap_layout.h
	$(CC) -E -P -x c -I$(@D) $< -o $@

.PHONY: shell
shell: $(PPC_USER_OUT)/shell.bin

$(PPC_USER_OUT)/shell.elf: $(filter-out $(PPC_USER_OUT)/syscalls.o,$(PPC_USER_CRT)) $(PPC_RES_OBJ) $(PPC_RES_DIR)/app_sys.ld sdk/link_guard.py
	$(PPC_GUARDED_LD) -m $(SDK_TARGET_ELF_EMULATION) --no-undefined --gc-sections -z noexecstack -T $(PPC_RES_DIR)/app_sys.ld -Map=$(@:.elf=.map) -o $@ $(filter %.o,$^) -L$(dir $(PPC_USER_LIBC)) -L$(dir $(PPC_USER_LIBGCC)) -lc -lgcc

$(PPC_USER_OUT)/shell.raw: $(PPC_USER_OUT)/shell.elf
	$(OBJCOPY) -O binary $< $@

$(PPC_USER_OUT)/shell.bin: $(PPC_USER_OUT)/shell.raw $(PPC_USER_OUT)/shell.elf sdk/mkos32x.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/shell.elf --heap 0

# The acceptance GUI uses the same resident CRT and fixed heap as the C shell.
PPC_GUI_SRC := $(wildcard userland/lib/gfx/*.c userland/lib/gfx/draw/*.c userland/lib/gfx/text/*.c userland/lib/cfg/*.c) lib/utf8.c
PPC_GUI_OBJ := $(addprefix $(PPC_RES_DIR)/,$(PPC_GUI_SRC:.c=.o))
$(PPC_GUI_OBJ): $(PPC_RES_DIR)/%.o: %.c build/ppc-user.mk $(SDK_KAPI_HDR)
	@mkdir -p $(@D)
	$(CC) $(PPC_USER_FLAGS) -ffunction-sections -fdata-sections -DOS32_CRT_RESIDENT -DOS32_GFX_PACKED_ONLY -I. -Iinclude -Iuserland/lib -Iuserland/lib/gfx -Iuserland/lib/cfg -Iuserland/lib/math -c $< -o $@

PPC_GUI_LIB := $(PPC_RES_DIR)/cargo/powerpc-os32-none/release/libgshell.a
.PHONY: ppc-gshell-cargo
PPC_GSHELL_DIR ?= userland/gshell
PPC_GUI_TRIAL ?= normal
$(PPC_GUI_LIB): ppc-gshell-cargo
	cd $(PPC_GSHELL_DIR) && PPC_GUI_TRIAL="$(PPC_GUI_TRIAL)" PATH="$(PPC_CROSS_DIR)/bin:$$PATH" OS32_PPC_RESIDENT=1 OS32_SDK_TARGET=$(SDK_TARGET) RUSTC_WRAPPER=$(CURDIR)/sdk/rustc_stamp.py cargo build --release $(if $(filter 1,$(PPC_C_GUI)),--features c_gui_accept) --target ../../sdk/rust/powerpc-os32-none.json --target-dir $(abspath $(PPC_RES_DIR)/cargo)

	@if ! test -f $(@D)/.c-gui-mode || [ "$$(cat $(@D)/.c-gui-mode)" != "[$(PPC_C_GUI) $(PPC_GUI_TRIAL)]" ]; then \
		echo "[$(PPC_C_GUI) $(PPC_GUI_TRIAL)]" > $(@D)/.c-gui-mode; touch "$@"; fi

$(PPC_USER_OUT)/gshell.elf: $(filter-out $(PPC_USER_OUT)/syscalls.o,$(PPC_USER_CRT)) $(PPC_RES_DIR)/sdk/crt/syscalls.o $(PPC_RES_DIR)/sdk/allocator/nano_adapter.o $(PPC_GUI_OBJ) $(PPC_GUI_LIB) $(PPC_RES_DIR)/app_sys.ld
	$(PPC_GUARDED_LD) -m $(SDK_TARGET_ELF_EMULATION) --no-undefined --gc-sections -z noexecstack -T $(PPC_RES_DIR)/app_sys.ld -Map=$(@:.elf=.map) -o $@ $(filter %.o %.a,$^) -L$(dir $(PPC_USER_LIBC)) -L$(dir $(PPC_USER_LIBGCC)) -lc -lgcc

$(PPC_USER_OUT)/gshell.raw: $(PPC_USER_OUT)/gshell.elf
	$(OBJCOPY) -O binary $< $@

$(PPC_USER_OUT)/gshell.bin: $(PPC_USER_OUT)/gshell.raw $(PPC_USER_OUT)/gshell.elf sdk/mkos32x.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/gshell.elf --heap 0 --gfx

-include $(PPC_GUI_OBJ:.o=.d)
-include $(PPC_RES_OBJ:.o=.d)

# The shell uses a private profile and object tree, isolated from x86 artifacts.
PPC_SH_DIR := $(PPC_USER_OUT)/sh
PPC_SH_SRC := $(wildcard userland/shell/*.c)
PPC_SH_OBJ := $(patsubst userland/shell/%.c,$(PPC_SH_DIR)/shell/%.o,$(PPC_SH_SRC))
# Acceptance may rebuild only the USER UTF-8 adapter from a reverted source.
# Test copies keep USER mutations out of the product tree.
PPC_SH_MUT_SRC ?=
PPC_SH_MUT_FILE ?=
PPC_SH_RUNTIME_SRC ?= userland/ppc/sh/newlib_runtime.c
PPC_SH_RT := $(addprefix $(PPC_SH_DIR)/,runtime.o)
PPC_SH_PROFILE ?= userland/ppc/sh/profile.h
PPC_SH_FLAGS := -Iplatform/powermac -include $(PPC_SH_PROFILE) $(PPC_USER_FLAGS) -I. -Iinclude -Isdk/include -Iuserland/lib -Iuserland/shell -Iuserland/lib/filer -Iuserland/lib/gfx -Idrivers

.PHONY: sh
sh: $(PPC_USER_OUT)/sh.bin

$(PPC_SH_DIR)/shell/%.o: userland/shell/%.c $(PPC_SH_MUT_FILE) $(PPC_SH_PROFILE) userland/ppc/sh/profile.h
	@mkdir -p $(@D)
	$(CC) $(PPC_SH_FLAGS) -c $(if $(filter $<,$(PPC_SH_MUT_SRC)),$(PPC_SH_MUT_FILE),$<) -o $@

$(PPC_SH_DIR)/runtime.o: $(PPC_SH_RUNTIME_SRC) $(PPC_SH_PROFILE) userland/ppc/sh/profile.h
	@mkdir -p $(@D)
	$(CC) $(PPC_SH_FLAGS) -Iuserland/ppc/sh -c $< -o $@

$(PPC_SH_DIR)/%.o: userland/ppc/sh/%.c $(PPC_SH_PROFILE) userland/ppc/sh/profile.h
	@mkdir -p $(@D)
	$(CC) $(PPC_SH_FLAGS) -c $< -o $@

$(PPC_SH_DIR)/ls.o: userland/lib/rt/ls.c
	@mkdir -p $(@D)
	$(CC) $(PPC_SH_FLAGS) -c $< -o $@

$(PPC_USER_OUT)/sh.elf: $(PPC_USER_CRT) $(PPC_SH_OBJ) $(PPC_SH_RT) $(SDK_TARGET_APP_LINK) sdk/link_guard.py sdk/allocator/check_link_target.py $(PPC_USER_NANO) $(PPC_USER_NANO:.a=.json)
	$(PPC_USER_LINK) -o $@ $(filter %.o,$^) $(PPC_USER_LIBS)

$(PPC_USER_OUT)/sh.raw: $(PPC_USER_OUT)/sh.elf
	$(OBJCOPY) -O binary $< $@

$(PPC_USER_OUT)/sh.bin: $(PPC_USER_OUT)/sh.raw $(PPC_USER_OUT)/sh.elf sdk/mkos32x.py sdk/os32x_hdr.py sdk/os32_generations.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/sh.elf --stack 65536 --heap 131072

-include $(PPC_SH_OBJ:.o=.d) $(PPC_SH_RT:.o=.d)
-include $(PPC_USER_OUT)/fp_probe.d $(PPC_USER_OUT)/fp_probe_asm.d
-include $(PPC_USER_CRT:.o=.d) $(addsuffix .d,$(PPC_USER_APPS))
# Flag/provider changes must rebuild old shell objects (notably stdout's ABI).
$(PPC_USER_CRT) $(addsuffix .o,$(PPC_USER_APPS)) $(PPC_SH_OBJ) $(PPC_SH_RT) $(PPC_USER_OUT)/fp_probe.o $(PPC_USER_OUT)/fp_probe_asm.o: build/ppc-user.mk
endif
