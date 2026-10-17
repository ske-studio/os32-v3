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

exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

exec/ring3_ls.o exec/exec.o: exec/ring3_ls.h

# === カーネル C ソース ===
C_KERNEL = \
    kernel/kernel.c kernel/kstack_hw.c kernel/boot_font.c kernel/gdt.c kernel/tss.c kernel/v86.c kernel/v86_mem.c kernel/v86_io.c kernel/v86_pic.c kernel/v86_kbd.c kernel/v86_bios.c kernel/v86_gcap.c kernel/boot_splash.c kernel/idt.c kernel/isr_handlers.c kernel/cpu_calibrate.c kernel/cpu_calibrate_math.c kernel/sysclk.c kernel/pit_math.c kernel/bootinfo.c kernel/bootinfo_check.c \
    kernel/paging.c kernel/paging_app.c kernel/dma_pool.c kernel/dma_pool_math.c kernel/physmem.c kernel/pgalloc.c kernel/ledger_pci.c kernel/memory_boot.c kernel/shlib.c kernel/shm.c kernel/gui.c kernel/kmalloc.c kernel/console.c kernel/con_sink.c kernel/bootlog.c kernel/bootlog_save.c kernel/kbd_inject.c kernel/sys.c kernel/sysconfig.c kernel/kaudit.c kernel/kaudit_boot.c kernel/kselftest_report.c kernel/irq.c kernel/irq_math.c kernel/ktime.c kernel/time_math.c \
    kernel/sqlite_engine.c kernel/sqlite_outer.c kernel/module.c kernel/module_image.c kernel/module_runtime.c kernel/unicode_file.c kernel/ime.c kernel/ime_romkana.c kernel/ime_dict.c kernel/ime_render_tvram.c kernel/snd_engine.c \
    drivers/kbd.c drivers/kbd_status.c drivers/kbd_dlog.c drivers/serial.c drivers/serial_plan.c drivers/fm.c \
    drivers/fdc.c drivers/fdc_decide.c drivers/fdc_track.c drivers/disk.c drivers/dma8237.c drivers/dma8237_math.c drivers/pci.c drivers/pci_decode.c drivers/pci_bind.c drivers/pci_bind_match.c drivers/e82557.c drivers/e82557_eeprom.c drivers/e82557_desc.c drivers/ide.c drivers/ide_addr.c drivers/pc98pt.c drivers/atapi.c drivers/rtc.c drivers/dev.c drivers/kcg.c drivers/np2sysp.c drivers/loop_dev.c \
    drivers/mouse.c drivers/mouse_bus.c drivers/mouse_seamless.c \
    drivers/lgy98.c drivers/ne2000.c drivers/ne2000_ring.c \
    drivers/pcm_cs4231.c drivers/pcm_cs4231_math.c \
    drivers/wab_glue_xe10.c drivers/wab_cirrus.c \
    net/link.c \
    gfx/gfx_core.c gfx/gfx_vram.c gfx/gfx_scroll.c gfx/palette.c gfx/backend_pc98.c gfx/backend_pegc.c gfx/backend_cirrus.c \
    fs/fatfs/ff.c fs/fatfs/diskio.c fs/fatfs_vfs.c \
    fs/ext2_super.c fs/ext2_inode.c fs/ext2_dir.c fs/ext2_file.c fs/ext2_fmt.c fs/ext2_layout.c fs/ext2_vfs.c fs/vfs.c fs/vfs_fd.c fs/fd_redirect.c fs/pipe_buffer.c fs/iso9660.c fs/hostdrvfs.c fs/sfs_proto.c fs/sfs_client.c fs/sfs_lan.c fs/serialfs.c fs/serialfs_session.c \
    exec/exec.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c exec/ring3_ls.c exec/redir_access.c exec/access_walk.c exec/surface_query.c exec/system_surface.c exec/lease.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c \
    kapi/kapi_print.c kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_host.c \
    lib/path.c lib/utf8.c lib/kprintf.c lib/kprintf_attr.c lib/os_time.c lib/kstring.c lib/kutf16.c lib/kmath.c lib/crc32.c \
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

# === SQLite関連 (core BSS の直後に連結、build/os32.ld) ===
C_SQLITE = lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_vfs.c lib/sqlite3/os32_sqlite_test.c
C_SQLITE_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(C_SQLITE:.c=.o))

# === モジュール別コンパイルルール ===

# kernel/ モジュール
kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# Resident debug loader and validator: keep their boot-only code compact.
kernel/module.o kernel/module_image.o kernel/module_runtime.o: private KERNEL_CFLAGS += -Os
kernel/module.o kernel/module_image.o kernel/module_runtime.o: build/kernel.mk

# drivers/ モジュール
# mouse.c は idt.h (IRQ制御) を参照するため INC_KERNEL でビルド
drivers/mouse.o: drivers/mouse.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# loop_dev.c は vfs.h / kprintf.h (fs/, lib/) を参照するため INC_KERNEL でビルド
drivers/loop_dev.o: drivers/loop_dev.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# LAN: ne2000.c は idt.h (tick_count) / cpu_calibrate.h、lgy98.c は idt.h (PIC) を
# 参照するため INC_KERNEL。lgy98.o は CONFIG_LGY98_* (make kernel LGY98=1) を
# 唯一読む場所なので、フラグ変更が拾えるよう毎回コンパイルする。
drivers/ne2000.o: drivers/ne2000.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# ATAPI: atapi.c は cpu_calibrate.h (cpu_delay_us / CPU_DELAY_US_MAX) を参照するため
# INC_KERNEL (ne2000.c と同じ)
drivers/atapi.o: drivers/atapi.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

drivers/lgy98.o: drivers/lgy98.c .FORCE
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# PCM: pcm_cs4231.c は irq.h / dma_pool.h / idt.h (kernel/) を参照するため
# INC_KERNEL。純粋部 (pcm_cs4231_math.c) は既定の drivers/%.o (-O2) で通る
# (票 TASK_PCM_CS4231 §2-1)。実装時の -Os は KHEAP 縮小 (予算 596KB) への
# 合流で外した (2026-09-23)。
drivers/pcm_cs4231.o: drivers/pcm_cs4231.c
	$(CC) $(CFLAGS_BASE) -O2 $(INC_KERNEL) -c $< -o $@

# f5a: keep internal appmem wiring within the f image budget.
exec/exec_heap.o exec/appmem.o exec/appmem_map.o exec/appmem_unmap.o: exec/%.o: exec/%.c
	$(CC) $(CFLAGS_BASE) -Os $(INC_EXEC) -c $< -o $@

kernel/paging_app.o: kernel/paging_app.c
	$(CC) $(CFLAGS_BASE) -Os $(INC_KERNEL) -c $< -o $@

# net/ (リンク層。ne2000.h / idt.h / kstring.h を参照するため INC_KERNEL)
net/%.o: net/%.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# R5 EEPROM runs once at boot; keep diagnostic formatting within the image budget.
drivers/e82557.o drivers/e82557_eeprom.o drivers/e82557_desc.o: drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS_BASE) -Os $(INC_DRIVERS) -c $< -o $@

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS_BASE) $(INC_DRIVERS) -c $< -o $@

# gfx/ モジュール
gfx/%.o: gfx/%.c
	$(CC) $(CFLAGS_BASE) $(INC_GFX) -c $< -o $@

# fs/ モジュール
fs/sfs_lan.o: fs/sfs_lan.h fs/sfs_client.h fs/sfs_proto.h
# セッションは常駐シェルの判定 (exec/appslot.h の APP_ID_SHELL) を見るので -Iexec も要る
fs/serialfs_session.o: fs/serialfs_session.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

fs/%.o: fs/%.c
	$(CC) $(CFLAGS_BASE) $(INC_FS) -c $< -o $@

# fs/fatfs/ モジュール (FatFs公式ソース — string.hスタブ付き)
fs/fatfs/ff.o: fs/fatfs/ff.c fs/fatfs/ffconf.h fs/fatfs/string.h
	$(CC) $(CFLAGS_BASE) $(INC_FATFS) -c $< -o $@

fs/fatfs/diskio.o: fs/fatfs/diskio.c fs/fatfs/ff.h fs/fatfs/diskio.h
	$(CC) $(CFLAGS_BASE) $(INC_FATFS) -c $< -o $@

# exec/ モジュール
exec/%.o: exec/%.c kapi/kapi_generated.c
	$(CC) $(CFLAGS_BASE) $(INC_EXEC) -c $< -o $@

# kapi/ モジュール
kapi/kapi_generated.c: sdk/kapi.json sdk/gen_kapi.py
	python3 sdk/gen_kapi.py

# kapi_sys.o は __DATE__/__TIME__ を含むため毎回再コンパイル
kapi/kapi_sys.o: kapi/kapi_sys.c .FORCE
	$(CC) $(CFLAGS_BASE) $(INC_KAPI) -c $< -o $@

kapi/%.o: kapi/%.c kapi/kapi_generated.c
	$(CC) $(CFLAGS_BASE) $(INC_KAPI) -c $< -o $@

.FORCE:

# lib/ モジュール (汎用ライブラリ: カーネル依存なし)
lib/%.o: lib/%.c
	$(CC) $(CFLAGS_BASE) $(INC_LIB) -c $< -o $@

# === Rust LZ4デコーダ (lib/os32_lz4/) ===
# C版 (lib/lz4.c) をRust実装に置き換え。境界チェック付きの安全な展開を保証。
RUST_LZ4_DIR = lib/os32_lz4
RUST_LZ4_LIB = $(RUST_LZ4_DIR)/target/i686-os32-none/release/libos32_lz4.a

$(RUST_LZ4_LIB): $(RUST_LZ4_DIR)/src/lib.rs $(RUST_LZ4_DIR)/Cargo.toml
	cd $(RUST_LZ4_DIR) && RUSTC_WRAPPER=$(CURDIR)/sdk/rustc_stamp.py cargo build --release

# SQLite (カーネル拡張域配置 — -Os必須)
lib/sqlite3/sqlite3.o: lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_config.h
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

lib/sqlite3/os32_sqlite_vfs.o: lib/sqlite3/os32_sqlite_vfs.c lib/sqlite3/os32_sqlite_vfs.h lib/sqlite3/os32_sqlite_config.h
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c lib/sqlite3/os32_sqlite_vfs.h lib/sqlite3/os32_sqlite_config.h
	$(CC) $(C_STD_SQLITE) -m32 -include sdk/include/os32/os32_unit_stamp.h -march=i386 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -mno-red-zone -O0 -fcommon -Wno-long-long -w $(DEPFLAGS) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

# === カーネルリンク ===
$(BUILD_OUT)/kernel.elf: $(BUILD_OUT)/os32.ld $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(RUST_LZ4_LIB)
	$(LD) $(LDFLAGS) -o $@ $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(RUST_LZ4_LIB) -lgcc

$(BUILD_OUT)/kernel.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary \
		--remove-section=.sqlite_text \
		--remove-section=.sqlite_rodata \
		--remove-section=.sqlite_data \
		--remove-section=.sqlite_bss \
		$< $@

# SQLite 拡張域バイナリ (ロード先は ELF の __sqlite_start)
$(BUILD_OUT)/sqlite.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary \
		--only-section=.sqlite_text \
		--only-section=.sqlite_rodata \
		--only-section=.sqlite_data \
		$< $@

# === VK32 圧縮カーネルイメージ ===
$(BUILD_OUT)/vmkernel.lz4: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) \
		--elf $(BUILD_OUT)/kernel.elf --kernel $(BUILD_OUT)/kernel.bin \
		--sqlite $(BUILD_OUT)/sqlite.bin \
		-o $(BUILD_OUT)/vmkernel.lz4

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
KERNEL_PAIRED_MODULES := $(BUILD_OUT)/kdebug.bin
kernel: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/layout-manifest.json $(KERNEL_PAIRED_MODULES)

# === カーネルクリーン ===
clean-kernel:
	rm -f $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_SRC) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(BUILD_OUT)/kernel.elf $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/kernel.map
	cd $(RUST_LZ4_DIR) && cargo clean 2>/dev/null || true

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
R1_C := $(C_KERNEL) kernel/r1_fixture.c
R1_OBJECTS := $(addprefix $(R1_OBJ)/,$(ASM_KERNEL:.asm=.o) exec/ring3_ls_shim.o $(R1_C:.c=.o)) $(R1_OBJ)/build_id.o $(addprefix $(R1_OBJ)/,$(C_SQLITE:.c=.o))
R1_SMALL := exec/exec_heap.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c kernel/paging_app.c

$(R1_OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(R1_SMALL)),-Os) -DOS32_R1_FIXTURE $(if $(filter fs/fatfs/%,$<),$(INC_FATFS),$(INC_KERNEL)) -c $< -o $@

$(R1_OBJ)/lib/sqlite3/%.o: lib/sqlite3/%.c lib/sqlite3/os32_sqlite_config.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_SQLITE) $(if $(filter %/os32_sqlite_test.c,$<),-O0) -DOS32_R1_FIXTURE -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

$(R1_OBJ)/%.o: %.asm sdk/crt/generations.inc
	@mkdir -p $(@D)
	$(LAYOUT_CPP) -traditional-cpp $< -o $@.asm
	$(AS) -p sdk/crt/generations.inc -f elf32 $@.asm -o $@

$(R1_OBJ)/exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h
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

$(R1_OUT)/kernel.elf: $(R1_OBJECTS) $(RUST_LZ4_LIB) $(R1_OUT)/os32.ld
	$(LD) $(filter-out -Map=% -T $(BUILD_OUT)/os32.ld,$(LDFLAGS)) -T $(R1_OUT)/os32.ld -Map=$(R1_OUT)/kernel.map -o $@ $(R1_OBJECTS) $(RUST_LZ4_LIB) -lgcc

$(R1_OUT)/kernel.bin: $(R1_OUT)/kernel.elf
	$(OBJCOPY) -O binary --remove-section=.sqlite_text --remove-section=.sqlite_rodata --remove-section=.sqlite_data --remove-section=.sqlite_bss $< $@

$(R1_OUT)/sqlite.bin: $(R1_OUT)/kernel.elf
	$(OBJCOPY) -O binary --only-section=.sqlite_text --only-section=.sqlite_rodata --only-section=.sqlite_data $< $@

$(R1_OUT)/vmkernel.lz4: $(R1_OUT)/kernel.bin $(R1_OUT)/sqlite.bin $(R1_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) --elf $(R1_OUT)/kernel.elf --kernel $(R1_OUT)/kernel.bin --sqlite $(R1_OUT)/sqlite.bin -o $@

R1_PAIRED_MODULES := $(R1_OUT)/kdebug.bin
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

$(BUILD_OUT)/os32.ld: $(BUILD_OUT)/os32.raw.ld
	sed -E 's@([a-zA-Z0-9_/]+\.o)@$(KERNEL_OBJ_PREFIX)\1@g' $< > $@

$(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(C_SQLITE_OBJ) $(BUILD_ID_OBJ) $(R1_OBJECTS): $(LAYOUT_HEADER) build/kernel.mk build/config.mk

# Keep the original source-adjacent legacy objects. All alternate output trees
# use separate objects/dependencies, including SQLite and assembler inputs.
ifneq ($(KERNEL_OBJ_PREFIX),)
LAYOUT_SMALL = $(R1_SMALL) kernel/module.c kernel/module_image.c kernel/module_runtime.c drivers/e82557.c drivers/e82557_eeprom.c drivers/e82557_desc.c
$(KERNEL_OBJ_PREFIX)%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) $(if $(filter $<,$(LAYOUT_SMALL)),-Os) $(if $(filter kernel/module.c,$<),-Ikdebug/generated) $(if $(filter fs/fatfs/%,$<),$(INC_FATFS),$(INC_KERNEL)) -c $< -o $@

$(KERNEL_OBJ_PREFIX)lib/sqlite3/%.o: lib/sqlite3/%.c lib/sqlite3/os32_sqlite_config.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_SQLITE) $(if $(filter %/os32_sqlite_test.c,$<),-O0) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

$(KERNEL_OBJ_PREFIX)exec/ring3_ls_shim.o: exec/ring3_ls.S exec/ring3_ls.h sdk/include/os32/os32_generations.h
	@mkdir -p $(@D)
	$(CC) -m32 -Isdk/include/os32 -c $< -o $@

$(KERNEL_OBJ_PREFIX)kernel/module.o: kdebug/generated/kdebug_imports.h

$(KERNEL_OBJ_PREFIX)exec/exec.o $(KERNEL_OBJ_PREFIX)kapi/kapi_generated.o: kapi/kapi_generated.c
$(KERNEL_OBJ_PREFIX)drivers/lgy98.o $(KERNEL_OBJ_PREFIX)kapi/kapi_sys.o: .FORCE
-include $(C_KERNEL_OBJ:.o=.d) $(C_SQLITE_OBJ:.o=.d)
endif

$(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o)): $(KERNEL_OBJ_PREFIX)%.o: %.asm sdk/crt/generations.inc
	@mkdir -p $(@D) $(dir $(BUILD_OUT)/asm/$<)
	$(LAYOUT_CPP) -traditional-cpp $< -o $(BUILD_OUT)/asm/$<
	$(AS) -p sdk/crt/generations.inc -f elf32 $(BUILD_OUT)/asm/$< -o $@
