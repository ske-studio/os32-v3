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

ASM_KERNEL_OBJ = $(ASM_KERNEL:.asm=.o)

# === カーネル C ソース ===
C_KERNEL = \
    kernel/kernel.c kernel/boot_font.c kernel/gdt.c kernel/tss.c kernel/v86.c kernel/v86_mem.c kernel/v86_io.c kernel/v86_pic.c kernel/v86_kbd.c kernel/v86_bios.c kernel/v86_gcap.c kernel/v86_gcap_math.c kernel/boot_splash.c kernel/idt.c kernel/isr_handlers.c kernel/cpu_calibrate.c kernel/cpu_calibrate_math.c kernel/sysclk.c kernel/pit_math.c kernel/bootinfo.c kernel/bootinfo_check.c \
    kernel/paging.c kernel/dma_pool.c kernel/dma_pool_math.c kernel/physmem.c kernel/pgalloc.c kernel/memory_boot.c kernel/shlib.c kernel/shm.c kernel/gui.c kernel/kmalloc.c kernel/console.c kernel/con_sink.c kernel/bootlog.c kernel/bootlog_save.c kernel/kbd_inject.c kernel/sys.c kernel/sysconfig.c kernel/kselftest.c kernel/irq.c kernel/irq_math.c kernel/ktime.c kernel/time_math.c \
    kernel/ime.c kernel/ime_romkana.c kernel/ime_dict.c kernel/ime_render_tvram.c kernel/snd_engine.c \
    drivers/kbd.c drivers/kbd_status.c drivers/kbd_dlog.c drivers/serial.c drivers/serial_plan.c drivers/fm.c \
    drivers/fdc.c drivers/fdc_decide.c drivers/fdc_track.c drivers/disk.c drivers/dma8237.c drivers/dma8237_math.c drivers/pci.c drivers/pci_decode.c drivers/pci_bind.c drivers/pci_bind_match.c drivers/ide.c drivers/ide_addr.c drivers/pc98pt.c drivers/atapi.c drivers/rtc.c drivers/dev.c drivers/kcg.c drivers/np2sysp.c drivers/loop_dev.c \
    drivers/mouse.c drivers/mouse_bus.c drivers/mouse_seamless.c \
    drivers/lgy98.c drivers/ne2000.c drivers/ne2000_ring.c \
    drivers/pcm_cs4231.c drivers/pcm_cs4231_math.c \
    drivers/wab_glue_xe10.c drivers/wab_cirrus.c \
    net/link.c \
    gfx/gfx_core.c gfx/gfx_vram.c gfx/gfx_scroll.c gfx/palette.c gfx/backend_pc98.c gfx/backend_pegc.c gfx/backend_cirrus.c \
    fs/fatfs/ff.c fs/fatfs/diskio.c fs/fatfs_vfs.c \
    fs/ext2_super.c fs/ext2_inode.c fs/ext2_dir.c fs/ext2_file.c fs/ext2_fmt.c fs/ext2_layout.c fs/ext2_vfs.c fs/vfs.c fs/vfs_fd.c fs/fd_redirect.c fs/pipe_buffer.c fs/iso9660.c fs/hostdrvfs.c fs/sfs_proto.c fs/sfs_client.c fs/serialfs.c fs/serialfs_session.c \
    exec/exec.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c \
    kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_host.c \
    lib/path.c lib/utf8.c lib/kprintf.c lib/kprintf_attr.c lib/os_time.c lib/kstring.c lib/kutf16.c lib/kmath.c lib/crc32.c \
    $(KSTRING_C_SRC)

C_KERNEL_OBJ = $(C_KERNEL:.c=.o)

# === コミット ID (票 TASK_SERIAL_HOSTFS A-4) ===
# 生成器は毎回走るが、中身が変わったときだけ build_id.c を書き直す。
# コミット ID が変わっても組み直すのは build_id.o 1 つとリンクだけ。
BUILD_ID_SRC = $(BUILD_OUT)/build_id.c
BUILD_ID_OBJ = $(BUILD_OUT)/build_id.o

$(BUILD_ID_SRC): .FORCE
	@mkdir -p $(BUILD_OUT)
	@python3 tools/gen_build_id.py -o $@

$(BUILD_ID_OBJ): $(BUILD_ID_SRC)
	$(CC) $(CFLAGS_BASE) -c $< -o $@

# === SQLite関連 (カーネル拡張域 0x200000 に配置) ===
C_SQLITE = lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_vfs.c lib/sqlite3/os32_sqlite_test.c
C_SQLITE_OBJ = $(C_SQLITE:.c=.o)

# === モジュール別コンパイルルール ===

# kernel/ モジュール
kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

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

# net/ (リンク層。ne2000.h / idt.h / kstring.h を参照するため INC_KERNEL)
net/%.o: net/%.c
	$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

# NE2000 受信リング計算のホスト試験 (I/O 無し)。make check から呼ばれる。
# I/O の実動作試験を代替しない (docs/tasks/network/PLAN.md §6)。
check-ne2000-ring:
	@mkdir -p $(BUILD_OUT)
	@gcc -std=gnu89 -Wall -Wextra -DNE2K_HOST_TEST -Idrivers \
	    -o $(BUILD_OUT)/ne2000_ring_test tools/tests/ne2000_ring_test.c drivers/ne2000_ring.c
	@$(BUILD_OUT)/ne2000_ring_test

.PHONY: check-ne2000-ring

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS_BASE) $(INC_DRIVERS) -c $< -o $@

# gfx/ モジュール
gfx/%.o: gfx/%.c
	$(CC) $(CFLAGS_BASE) $(INC_GFX) -c $< -o $@

# fs/ モジュール
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
	cd $(RUST_LZ4_DIR) && cargo build --release

# SQLite (カーネル拡張域配置 — -Os必須)
lib/sqlite3/sqlite3.o: lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_config.h
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

lib/sqlite3/os32_sqlite_vfs.o: lib/sqlite3/os32_sqlite_vfs.c lib/sqlite3/os32_sqlite_vfs.h lib/sqlite3/os32_sqlite_config.h
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c lib/sqlite3/os32_sqlite_vfs.h lib/sqlite3/os32_sqlite_config.h
	$(CC) -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -mno-red-zone -O0 -fcommon -Wno-long-long -w $(DEPFLAGS) -include lib/sqlite3/os32_sqlite_config.h $(INC_SQLITE) -c $< -o $@

# === カーネルリンク ===
$(BUILD_OUT)/kernel.elf: $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(RUST_LZ4_LIB)
	$(LD) $(LDFLAGS) -o $@ $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(RUST_LZ4_LIB) -lgcc

$(BUILD_OUT)/kernel.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary \
		--remove-section=.sqlite_text \
		--remove-section=.sqlite_rodata \
		--remove-section=.sqlite_data \
		--remove-section=.sqlite_bss \
		$< $@

# SQLite 拡張域バイナリ (0x200000 にロードされる)
$(BUILD_OUT)/sqlite.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary \
		--only-section=.sqlite_text \
		--only-section=.sqlite_rodata \
		--only-section=.sqlite_data \
		$< $@

# === VK32 圧縮カーネルイメージ ===
$(BUILD_OUT)/vmkernel.lz4: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin
	python3 tools/mkvmkernel.py \
		--kernel $(BUILD_OUT)/kernel.bin --kernel-addr 0x100000 \
		--sqlite $(BUILD_OUT)/sqlite.bin --sqlite-addr 0x200000 \
		-o $(BUILD_OUT)/vmkernel.lz4

# === カーネル単独ターゲット ===
kernel: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/vmkernel.lz4

# === カーネルクリーン ===
clean-kernel:
	rm -f $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_SRC) $(BUILD_ID_OBJ) $(C_SQLITE_OBJ) $(BUILD_OUT)/kernel.elf $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/kernel.map
	cd $(RUST_LZ4_DIR) && cargo clean 2>/dev/null || true

.PHONY: kernel clean-kernel

# LAN (LGY-98) 有効カーネル。emu_agent の許可リストから呼べる 1 語ターゲット。
# スタンプ (build/config.mk LGY98_STAMP) を置いてからビルドするので、以後の
# make kernel / deploy-nhd も LAN 有効のまま。kernel-nolgy98 で既定 (無効) に戻す。
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
