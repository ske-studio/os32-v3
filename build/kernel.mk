# ============================================================================
#  kernel.mk — カーネル + SQLite ビルドルール
# ============================================================================

# === カーネル ASM ソース ===
ASM_KERNEL_PRE  = kernel/kentry.asm kernel/isr_stub.asm kernel/v86_entry.asm kernel/ring3_entry.asm kernel/setjmp.asm
ASM_KERNEL_POST = lib/sqlite3/sqlite_stack.asm drivers/ne2000_io.asm

# kstring の 13 本 (kmemcpy / memcpy / kmemset / memset / kstrlen / strlen /
# kstrcmp / strcmp / kstrncmp / strncmp / kstrcpy / kstrncpy / memcmp) は
# x86 では lib/kstring_asm.asm、それ以外の ARCH では同じ契約の C 版
# lib/kstring_c.c を積む (移植準備 順序 4-b)。
#
# **x86 の既定はアセンブリのまま。** C 版に切り替えるかどうかは速度の実測が
# いる別の判断で、ここではまだ変えない。両版が同じ結果を返すことは
# tools/tests/test_kstring_c.py が両方を同じ実行ファイルにリンクして照合する。
ifeq ($(ARCH),x86)
ASM_KERNEL    = $(ASM_KERNEL_PRE) lib/kstring_asm.asm $(ASM_KERNEL_POST)
KSTRING_C_SRC =
else
ASM_KERNEL    = $(ASM_KERNEL_PRE) $(ASM_KERNEL_POST)
KSTRING_C_SRC = lib/kstring_c.c
endif

ASM_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o) exec/ring3_ls_shim.o)

# === カーネル C ソース ===
C_KERNEL = \
    kernel/boot_platform.c kernel/boot_display.c platform/pc98/emergency.c platform/pc98/boot.c kernel/font_guard.c kernel/kernel.c kernel/kstack_hw.c kernel/gdt.c kernel/double_fault.c kernel/tss.c kernel/v86.c kernel/v86_mem.c kernel/v86_snapshot.c kernel/v86_module.c kernel/v86_gcap.c kernel/kdebug_call.c kernel/boot_splash.c platform/pc98/splash.c kernel/idt.c kernel/isr_handlers.c kernel/cpu_calibrate.c kernel/cpu_calibrate_math.c kernel/sysclk.c platform/pc98/clock.c kernel/pit.c platform/pc98/timer.c kernel/pit_math.c kernel/bootinfo.c kernel/bootinfo_check.c \
    kernel/paging.c kernel/paging_app.c kernel/dma_pool.c kernel/dma_pool_math.c kernel/physmem.c kernel/pgalloc.c platform/pc98/lan_match.c platform/pc98/device_loop.c platform/pc98/device_ide.c platform/pc98/device_fs.c platform/pc98/device_pci.c platform/pc98/device_host.c platform/pc98/device_kcg.c platform/pc98/pci_resources.c platform/pc98/boot_disk.c kernel/memory_boot.c platform/pc98/memory.c kernel/shlib.c kernel/shm.c kernel/gui.c kernel/input_service.c kernel/clock_service.c kernel/port_service.c kernel/serial_diag.c platform/pc98/d2_bind.c platform/pc98/boot_names.c kernel/kmalloc.c kernel/console.c platform/pc98/console.c platform/pc98/display.c kernel/con_sink.c kernel/bootlog.c kernel/bootlog_save.c kernel/kbd_inject.c kernel/sys.c platform/pc98/system.c platform/pc98/pic.c kernel/sysconfig.c kernel/kaudit.c kernel/kaudit_boot.c kernel/kselftest_report.c kernel/irq.c kernel/irq_math.c kernel/tick.c kernel/ktime.c kernel/time_math.c \
    kernel/ver_collect.c kernel/ver_format.c \
    kernel/font_source.c kernel/sqlite_engine.c kernel/sqlite_outer.c lib/sqlite3/os32_sqlite_test.c kernel/module.c kernel/hwdev_module.c kernel/module_image.c kernel/module_runtime.c kernel/unicode_file.c kernel/fep_module.c kernel/cd_module.c kernel/kcg_module.c kernel/fm_module.c \
    drivers/kbd.c drivers/kbd_status.c drivers/kbd_dlog.c drivers/serial.c drivers/serial_plan.c \
    drivers/fdc.c drivers/fdc_decide.c drivers/fdc_track.c drivers/dma8237.c drivers/dma8237_math.c lib/dma_contract.c drivers/pci.c drivers/pci_decode.c drivers/pci_bind.c drivers/pci_bind_match.c drivers/ide.c drivers/ide_addr.c drivers/pc98pt.c drivers/rtc.c drivers/dev.c drivers/kcg.c drivers/np2sysp.c drivers/loop_dev.c \
    drivers/mouse.c drivers/mouse_bus.c drivers/mouse_seamless.c \
    drivers/lgy98.c drivers/ne2000.c drivers/ne2000_ring.c \
    kernel/pcm_module.c kernel/lan_module.c \
    kernel/cirrus_module.c kernel/geforce_module.c kernel/geforce_platform.c gfx/pc98_route.c gfx/cirrus_identify.c \
    net/link.c \
    gfx/gfx_core.c gfx/gfx_vram.c gfx/gfx_scroll.c gfx/palette.c gfx/backend_pc98.c gfx/backend_pegc.c \
    fs/fatfs/ff.c fs/fatfs/diskio.c fs/fatfs_vfs.c \
    fs/ext2_super.c fs/ext2_inode.c fs/ext2_dir.c fs/ext2_file.c fs/ext2_fmt.c fs/ext2_layout.c fs/ext2_vfs.c fs/vfs.c fs/vfs_fd.c fs/fd_redirect.c fs/pipe_buffer.c fs/hostdrvfs.c fs/sfs_proto.c fs/sfs_client.c fs/serialfs.c fs/serialfs_session.c \
    exec/exec.c exec/shell_service.c exec/v86_call.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c exec/ring3_ls.c exec/redir_access.c exec/access_walk.c exec/surface_query.c exec/system_surface.c exec/lease.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c \
    kapi/kapi_print.c kapi/kapi_strings.c kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_host.c \
    lib/path.c lib/utf8.c lib/kprintf.c lib/kprintf_attr.c platform/pc98/console_attr.c lib/os_time.c lib/kstring.c lib/kutf16.c lib/kmath.c lib/crc32.c \
    $(KSTRING_C_SRC)

C_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(C_KERNEL:.c=.o))

# === コミット ID (票 TASK_SERIAL_HOSTFS A-4) ===
# 生成器は毎回走るが、中身が変わったときだけ build_id.c を書き直す。
# コミット ID が変わっても組み直すのは build_id.o 1 つとリンクだけ。
BUILD_ID_SRC = $(BUILD_OUT)/build_id.c
BUILD_ID_OBJ = $(BUILD_OUT)/build_id.o

$(BUILD_ID_SRC): .FORCE
	@mkdir -p $(BUILD_OUT)
	@python3 tools/gen_build_id.py -o $@

$(BUILD_ID_OBJ): $(BUILD_ID_SRC)
	$(CC) $(CFLAGS_BASE) -Iinclude -c $< -o $@

# Test-only real guard injection: 0=off, 1=kernel, 2=SQLite engine.
DF_GUARD_TEST ?= 0
$(KERNEL_OBJ_PREFIX)kernel/double_fault.o: private KERNEL_CFLAGS += -Os -DOS32_DF_GUARD_TEST=$(DF_GUARD_TEST)
$(KERNEL_OBJ_PREFIX)kernel/double_fault.o: .FORCE

# Resident debug loader and validator: preserve the source-adjacent flags.
$(addprefix $(KERNEL_OBJ_PREFIX),kernel/module.o kernel/module_image.o kernel/module_runtime.o): private KERNEL_CFLAGS += -Os

# 常駐 shell の内部入口 (T7b-b2、exec/shell_service.c)。wrapper は
# __builtin_return_address(0) で呼び手を確かめるので、このファイルだけ
# 尾呼出しを封じて組む (設計 §3-2)。
SHELL_SERVICE_OBJ = $(KERNEL_OBJ_PREFIX)exec/shell_service.o
$(SHELL_SERVICE_OBJ): private KERNEL_CFLAGS += -fno-optimize-sibling-calls
# S3b から C_KERNEL に入る (結線は exec/exec.c)。旗が付くことは
# `make -n` で確かめる (tools/tests/test_shell_service.py の静的検査も見る)。

# kapi/ モジュール
KAPI_GENERATED := kapi/kapi_generated.c exec/exec_kapi_init.inc \
    sdk/include/os32/os32_kapi_generated.h sdk/include/os32/os32_kapi_slots.h \
    sdk/include/os32/os32_generations.h sdk/include/os32/os32_unit_stamp.h \
    sdk/os32_generations.py sdk/rust/os32api/src/generations.rs \
    sdk/crt/generations.inc sdk/link/generations.ld \
    sdk/link/app.ld sdk/link/app_sys.ld sdk/link/shlib.ld build/os32.ld
$(KAPI_GENERATED) &: sdk/kapi.json sdk/gen_kapi.py
	python3 sdk/gen_kapi.py

# GCC honors SOURCE_DATE_EPOCH for __DATE__/__TIME__ in kapi_sys.c.
# Without that environment variable the normal wall-clock build date remains.
.FORCE:

$(BUILD_OUT)/kernel.elf: $(BUILD_OUT)/os32.ld $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ)
	$(LD) $(LDFLAGS) -o $@ $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) -lgcc

$(BUILD_OUT)/kernel.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# VK32 now contains only the resident core; SQLite is a paired /boot module.
$(BUILD_OUT)/vmkernel.lz4: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) \
		--elf $(BUILD_OUT)/kernel.elf --kernel $(BUILD_OUT)/kernel.bin -o $@

# === カーネル単独ターゲット ===
# ELF → bin → VK32 → 世代 → 配置の順。配備だけの再リンクでも照合表を更新する。
$(BUILD_OUT)/generations-manifest.json: $(BUILD_OUT)/vmkernel.lz4 tools/gen_generation_manifest.py sdk/kapi.json .FORCE
	python3 tools/gen_generation_manifest.py

$(BUILD_OUT)/layout-manifest.json: $(BUILD_OUT)/generations-manifest.json tools/gen_memmap.py $(LAYOUT_HEADER)
	python3 tools/gen_memmap.py --manifest --write

# カーネルを運ぶ経路は、再リンク後の世代表・配置表まで確定してから配備する。
# 生成規則と一緒に結線し、deploy.mk 単独の利用者には生成済み成果物を渡せる。
deploy deploy-u3 deploy-kernel deploy-nhd nhd-migrate-pt deploy-fd: $(BUILD_OUT)/layout-manifest.json

# 像の CRC (pair) を埋める部品の一覧。T4c-1 で SQLite、T5 で起動時の部品を足す。
KERNEL_PAIRED_MODULES := $(BUILD_OUT)/kdebug.bin $(BUILD_OUT)/sqlite.mod $(BUILD_OUT)/fep.mod $(BUILD_OUT)/cd.mod $(BUILD_OUT)/kcg.mod $(BUILD_OUT)/fm.mod $(BUILD_OUT)/pcm.mod $(BUILD_OUT)/v86.mod $(BUILD_OUT)/lan.mod $(BUILD_OUT)/cirrus.mod $(BUILD_OUT)/hwdev.mod $(BUILD_OUT)/geforce.mod
kernel: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/layout-manifest.json $(KERNEL_PAIRED_MODULES)

# === カーネルクリーン ===
clean-kernel:
	rm -f $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(ASM_KERNEL:.asm=.o) $(C_KERNEL:.c=.o) exec/ring3_ls_shim.o $(BUILD_ID_SRC) $(BUILD_ID_OBJ) $(BUILD_OUT)/kernel.elf $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/kernel.map

.PHONY: kernel clean-kernel

# LAN (LGY-98) 有効カーネル。emu_agent の許可リストから呼べる 1 語ターゲット。
# スタンプ (build/config.mk LGY98_STAMP) を置いてからビルドするので、以後の
# make kernel / deploy-nhd も LAN 有効のまま。kernel-nolgy98 で無効化する (stamp 無しの既定は有効)。
kernel-lgy98:
	@mkdir -p $(BUILD_OUT)
	@echo 5 > $(LGY98_STAMP)
	$(MAKE) kernel

# リンク層 L0 試験用 (DIAG 1 + LINKTEST 8 = 9、反射なし)。Host Agent
# (tools/host_agent.py) を先に起動しておくこと。
kernel-lgy98-link:
	@mkdir -p $(BUILD_OUT)
	@echo 9 > $(LGY98_STAMP)
	$(MAKE) kernel

kernel-nolgy98:
	@mkdir -p $(BUILD_OUT)
	@echo off > $(LGY98_STAMP)
	$(MAKE) kernel

.PHONY: kernel-lgy98 kernel-lgy98-link kernel-nolgy98

# M2 の対向試験 (inject → 受信 → 反射送信 → capture)。NP21/W が LAN 有効 + 反射モードの
# カーネルで起動していること。make check には入れない (実機が要る)。
check-net-m2:
	@python3 tools/net_m2_test.py

# M3: CPL3 プログラム常駐中に同じ試験を回す。aidebug のデバッグサーバは単一スレッドで
# 要求を 1 つずつ処理するため、戻ってこないコマンド (sleep 等) を /api/cmd で投げると
# inject / capture が詰まって試験にならない。less のような対話プログラムなら最初の画面で
# キー待ちに入って /api/cmd が戻り (チャネルが空く)、プログラムは CPL3 に常駐したままになる。
# → NIC IRQ がアプリの CR3 が有効な状態で配送・処理されることを確認する。
check-net-m2-cpl3:
	@python3 tools/net_m2_test.py --during-cmd "less /etc/profile" --wait-for "/etc/profile" --exit-key q --during-timeout 30

# M4: フロー制御なしでリングを故意に飽和させ、ドライバが wedge せず 100Hz
# ウォッチドッグで自己回復することを確認する (溢れても壊れない安全網)。
check-net-m4:
	@python3 tools/net_m4_test.py

# L0: リンク層 (Stop-and-Wait) の HELLO + PING/PONG 10 往復を確認する。
# 先に tools/host_agent.py を NP2NETSOCK ポートで起動し、kernel-lgy98-link を配備する。
check-net-l0:
	@python3 tools/net_l0_test.py

# L1: WINDOW/Credit のフロー制御。ホストに 200 DATA を流させ、絶対値 WINDOW で
# リングを溢れさせずに全部・順序どおり受けられることを確認する (host_agent.py が対向)。
check-net-l1:
	@python3 tools/net_l1_test.py

# L2: ストリーミング。128KB を 8KB バッファで順次消費し、欠落を Go-Back-N で回復する。
check-net-l2:
	@python3 tools/net_l2_test.py

# L3: Host Services。ホストに GET (本文をストリーム) / TIME (RPC) をさせ、OS32 が
# 要求を出して結果だけ受け取ることを確認する。
check-net-l3:
	@python3 tools/net_l3_test.py

.PHONY: check-net-m2 check-net-m2-cpl3 check-net-m4 check-net-l0 check-net-l1 check-net-l2 check-net-l3

# T2h: entirely separate objects, dependencies, build ID, map and image.
R1_OUT := $(BUILD_OUT)/r1
R1_OBJ := $(R1_OUT)/obj
$(R1_OBJ)/exec/shell_service.o: private KERNEL_CFLAGS += -fno-optimize-sibling-calls
R1_C := $(C_KERNEL) kernel/r1_fixture.c kernel/font_guard_fixture.c
R1_OBJECTS := $(addprefix $(R1_OBJ)/,$(ASM_KERNEL:.asm=.o) exec/ring3_ls_shim.o $(R1_C:.c=.o)) $(R1_OBJ)/build_id.o
R1_SMALL := exec/exec_heap.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c kernel/paging_app.c

$(R1_OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(R1_SMALL)),-Os) -DOS32_R1_FIXTURE $(if $(filter fs/fatfs/%,$<),$(INC_FATFS),$(INC_KERNEL)) -c $< -o $@

$(R1_OBJ)/%.o: %.asm sdk/crt/generations.inc kernel/cpu_flags.inc kernel/entry_layout.inc
	@mkdir -p $(@D)
	$(LAYOUT_CPP) -traditional-cpp $< -o $@.asm
	$(AS) -p sdk/crt/generations.inc -f elf32 $@.asm -o $@

$(R1_OBJ)/exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h include/cpu.h kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

$(R1_OUT)/build_id.c: .FORCE
	@mkdir -p $(@D)
	@python3 tools/gen_build_id.py -o $@

$(R1_OBJ)/build_id.o: $(R1_OUT)/build_id.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -DOS32_R1_FIXTURE -Iinclude -c $< -o $@

$(R1_OBJ)/exec/exec.o $(R1_OBJ)/kapi/kapi_generated.o: kapi/kapi_generated.c
$(R1_OBJ)/drivers/lgy98.o $(R1_OBJ)/kapi/kapi_sys.o: .FORCE

$(R1_OUT)/os32.ld: $(BUILD_OUT)/os32.raw.ld
	@mkdir -p $(@D)
	sed -E 's@([a-zA-Z0-9_/]+\.o)@$(R1_OBJ)/\1@g' $< > $@

$(R1_OUT)/kernel.elf: $(R1_OBJECTS) $(R1_OUT)/os32.ld
	$(LD) $(filter-out -Map=% -T $(BUILD_OUT)/os32.ld,$(LDFLAGS)) -T $(R1_OUT)/os32.ld -Map=$(R1_OUT)/kernel.map -o $@ $(R1_OBJECTS) -lgcc

$(R1_OUT)/kernel.bin: $(R1_OUT)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(R1_OUT)/vmkernel.lz4: $(R1_OUT)/kernel.bin $(R1_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) --elf $(R1_OUT)/kernel.elf --kernel $(R1_OUT)/kernel.bin -o $@

R1_PAIRED_MODULES := $(patsubst $(BUILD_OUT)/%,$(R1_OUT)/%,$(KERNEL_PAIRED_MODULES))
kernel-r1: $(R1_OUT)/vmkernel.lz4 $(R1_PAIRED_MODULES)
	python3 tools/r1_manifest.py $(R1_OUT)

-include $(R1_OBJECTS:.o=.d)
.PHONY: kernel-r1

# One generated header drives C, ld and NASM; FORCE re-runs the generator
# while write-if-changed keeps an unchanged build incremental.
$(LAYOUT_HEADER): include/memmap.h tools/gen_memmap.py .FORCE
	@python3 tools/gen_memmap.py --layout-header $@

$(BUILD_OUT)/os32.raw.ld: build/os32.ld $(LAYOUT_HEADER)
	$(LAYOUT_CPP) $< -o $@

$(BUILD_OUT)/os32.ld: $(BUILD_OUT)/os32.raw.ld build/config.mk build/kernel.mk
	sed -E 's@([a-zA-Z0-9_/]+\.o)@$(KERNEL_OBJ_PREFIX)\1@g' $< > $@

$(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(R1_OBJECTS): $(LAYOUT_HEADER) build/kernel.mk build/config.mk

# Core objects always live under BUILD_OUT. Preserve each original -I order;
# the R1 fixture above deliberately keeps its existing INC_KERNEL profile.
LAYOUT_SMALL = $(R1_SMALL)
$(KERNEL_OBJ_PREFIX)%.o: private KERNEL_INCLUDES = $(INC_KERNEL)
$(KERNEL_OBJ_PREFIX)drivers/%.o: private KERNEL_INCLUDES = $(INC_DRIVERS)
$(KERNEL_OBJ_PREFIX)gfx/%.o: private KERNEL_INCLUDES = $(INC_GFX)
$(KERNEL_OBJ_PREFIX)fs/%.o: private KERNEL_INCLUDES = $(INC_FS)
$(KERNEL_OBJ_PREFIX)fs/fatfs/%.o: private KERNEL_INCLUDES = $(INC_FATFS)
$(KERNEL_OBJ_PREFIX)exec/%.o: private KERNEL_INCLUDES = $(INC_EXEC)
$(KERNEL_OBJ_PREFIX)kapi/%.o: private KERNEL_INCLUDES = $(INC_KAPI)
$(KERNEL_OBJ_PREFIX)lib/%.o: private KERNEL_INCLUDES = $(INC_LIB)
CORE_KERNEL_INCLUDES = drivers/mouse.o drivers/loop_dev.o drivers/ne2000.o drivers/lgy98.o drivers/fdc.o fs/serialfs_session.o lib/sqlite3/os32_sqlite_test.o
$(addprefix $(KERNEL_OBJ_PREFIX),$(CORE_KERNEL_INCLUDES)): private KERNEL_INCLUDES = $(INC_KERNEL)

$(KERNEL_OBJ_PREFIX)%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(LAYOUT_SMALL)),-Os) $(KERNEL_INCLUDES) -c $< -o $@

$(KERNEL_OBJ_PREFIX)exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h include/cpu.h kernel/idt.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

$(addprefix $(KERNEL_OBJ_PREFIX),exec/ring3_ls.o exec/exec.o): exec/ring3_ls.h
$(addprefix $(KERNEL_OBJ_PREFIX),$(filter exec/%.o kapi/%.o,$(C_KERNEL:.c=.o))): kapi/kapi_generated.c
$(KERNEL_OBJ_PREFIX)drivers/lgy98.o $(KERNEL_OBJ_PREFIX)kapi/kapi_sys.o: .FORCE
$(KERNEL_OBJ_PREFIX)fs/fatfs/ff.o: fs/fatfs/ffconf.h fs/fatfs/string.h
$(KERNEL_OBJ_PREFIX)fs/fatfs/diskio.o: fs/fatfs/ff.h fs/fatfs/diskio.h

$(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o)): $(KERNEL_OBJ_PREFIX)%.o: %.asm sdk/crt/generations.inc kernel/cpu_flags.inc kernel/entry_layout.inc
	@mkdir -p $(@D) $(dir $(BUILD_OUT)/asm/$<)
	$(LAYOUT_CPP) -traditional-cpp $< -o $(BUILD_OUT)/asm/$<
	$(AS) -p sdk/crt/generations.inc -f elf32 $(BUILD_OUT)/asm/$< -o $@

# The boot selftest stays in core and calls the same typed facade as DB/FEP.
$(KERNEL_OBJ_PREFIX)lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# Finish every paired image before snapshotting the boot distribution.
$(BUILD_OUT)/generations-manifest.json: $(KERNEL_PAIRED_MODULES)
