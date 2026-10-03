# ============================================================================
# programs.mk — 外部プログラム (OS32X) ビルドルール
# ============================================================================

# === ベースプログラム (単体ソースファイル → 自動ビルド) ===
C_CMDS = $(wildcard userland/cmds/*.c)
C_TESTS = $(filter-out userland/tests/gfx200_test.c userland/tests/gfx_demo200.c userland/tests/blit_test.c userland/tests/blit_test2.c userland/tests/demo_tile.c userland/tests/tile_bench.c userland/tests/rotate_test.c userland/tests/db_test.c userland/tests/dbq.c userland/tests/e2test.c userland/tests/math_test.c userland/tests/input_test.c userland/tests/asset_test.c userland/tests/asset_demo.c userland/tests/ecs_test.c userland/tests/save_test.c userland/tests/mgx_test.c userland/tests/kbd_echo.c userland/tests/ring3_hello.c userland/tests/ring3_fault.c userland/tests/ring3_guard.c userland/tests/kstr_bench.c, $(wildcard userland/tests/*.c))
C_SYSTEM = $(filter-out userland/system/lz4.c userland/system/cdinst.c $(INST_SHARED_SRC), $(wildcard userland/system/*.c))
# cdinst / install が共有する hd0 の検査と書き込み (票 TASK_HDD_INSTALL 段 2)。
# main を持たないので単体のプログラムにはしない (下の INST_OBJ でリンクする)
INST_SHARED_SRC = userland/system/inst_disk.c userland/system/inst_hdd.c

C_BASE_PROGRAMS = $(C_CMDS) $(C_TESTS) $(C_SYSTEM)
BASE_PROGRAMS_BIN = $(C_BASE_PROGRAMS:.c=.bin) userland/shell.bin userland/tests/h2_stack512.bin

# === CRT0 ビルドルール ===
sdk/crt/crt0.o: sdk/crt/crt0.asm
	$(AS) -f elf32 $< -o $@

sdk/crt/help.o: sdk/crt/help.c sdk/include/os32/help.h $(SDK_KAPI_HDR)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

sdk/crt/crt0_c.o: sdk/crt/crt0_c.c sdk/include/os32/help.h $(SDK_KAPI_HDR)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

sdk/crt/syscalls.o: sdk/crt/syscalls.c $(SDK_KAPI_HDR)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/lib/rt/dbgserial.o: userland/lib/rt/dbgserial.c userland/lib/rt/dbgserial.h $(SDK_KAPI_HDR)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

# === Shell Module ===
SHELL_SRC = $(wildcard userland/shell/*.c)
SHELL_OBJ = $(SHELL_SRC:.c=.o)

# ヘッダと .inc の明示依存 (B7)。Makefile の DEPFILES は boot/kernel/... しか
# 走査しないので userland の .d は読まれない。sh_launch.inc / sh_pipe.inc /
# sh_redraw.inc を直しても .o が作り直されないと、直したつもりの sh.bin が
# 出来上がる。常駐側にも同じ依存を足す (レシピは変えないので .o は不変)。
SHELL_DEPS = userland/shell/shell.h userland/shell/hdprep_plan.h drivers/pc98pt.h $(wildcard userland/shell/*.inc)

userland/shell/%.o: userland/shell/%.c $(SHELL_DEPS)
	$(CC) $(PROGRAM_FLAGS) -Iuserland/shell $(INC_libos32filer) -c $< -o $@

# PCI の純粋な復号 (drivers/pci_decode.c) を `lspci` / `pcidump` からも使う。
# **写さない** — BAR の種別と番地、クラス名、Header Type の切り出しは
# カーネルと 1 文字も違ってはいけない。ホスト試験 (check-pci-decode-host) が
# 見張っているのはこの 1 本だけなので、CPL=3 側でも同じソースをもう一度
# コンパイルして繋ぐ。I/O も静的配列も触らないのでそのまま通る。
PCI_DECODE_USER_OBJ = userland/shell/pci_decode_user.o
$(PCI_DECODE_USER_OBJ): drivers/pci_decode.c drivers/pci_decode.h
	$(CC) $(PROGRAM_FLAGS) -Idrivers -c drivers/pci_decode.c -o $@

# 区画表の共有部 (drivers/pc98pt.c) も同じ扱い — `hdprep` が書く区画表は
# カーネル・ローダ・nhd_deploy.py と 1 バイトも違ってはいけない
# (票 TASK_HDD_INSTALL 段 1-4)。
PC98PT_USER_OBJ = userland/shell/pc98pt_user.o
$(PC98PT_USER_OBJ): drivers/pc98pt.c drivers/pc98pt.h
	$(CC) $(PROGRAM_FLAGS) -Idrivers -c drivers/pc98pt.c -o $@

userland/shell.elf: sdk/link/app_sys.ld $(CRT0_OBJ) $(SHELL_OBJ) $(PCI_DECODE_USER_OBJ) $(PC98PT_USER_OBJ) $(FILER_DRAW_OBJ)
	$(LD) -m elf_i386 -T sdk/link/app_sys.ld -nostdlib --nmagic --gc-sections -L$(LIBDIR) -L$(CROSS_DIR)/i386-elf/lib -L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0 -o $@ $(CRT0_OBJ) $(SHELL_OBJ) $(PCI_DECODE_USER_OBJ) $(PC98PT_USER_OBJ) $(LGRP_BEG) $(FILER_DRAW_OBJ) -los32save $(LGRP_END) -lc -lgcc

# === sh — 同じシェルのソースを CPL=3 の外部アプリとして (票 T9 D1) ===
# 常駐 shell.bin (app_sys.ld = 0x300000) の規則は上のまま一切変えない。同じ
# $(SHELL_SRC) を -DSHELL_AS_APP 付きで**専用の出力先** $(SH_OBJDIR) へ
# コンパイルし、crt0 + sdk/link/app.ld (0x500000) でリンクして sh.bin にする。
# .o を常駐の $(SHELL_OBJ) と混ぜないのが肝 — 混ぜると -DSHELL_AS_APP 付きの
# .o が shell.bin に流れ込む。受入 S7 は「shell.bin の SHA-256 が変更前後で
# 一致」なので、出力先もフラグも常駐側とは完全に分ける。
# 追加 OBJ (FILER_DRAW_OBJ / -los32save) は常駐と同じものをリンクする。
SH_OBJDIR = userland/shell/sh_obj
SH_OBJ = $(patsubst userland/shell/%.c,$(SH_OBJDIR)/%.o,$(SHELL_SRC))

$(SH_OBJDIR)/%.o: userland/shell/%.c $(SHELL_DEPS)
	@mkdir -p $(SH_OBJDIR)
	$(CC) $(PROGRAM_FLAGS) -DSHELL_AS_APP -Iuserland/shell $(INC_libos32filer) -c $< -o $@

# 常駐側と同じ drivers/pci_decode.c を、sh.bin 用の出力先へ。
SH_PCI_DECODE_OBJ = $(SH_OBJDIR)/pci_decode_user.o
$(SH_PCI_DECODE_OBJ): drivers/pci_decode.c drivers/pci_decode.h
	@mkdir -p $(SH_OBJDIR)
	$(CC) $(PROGRAM_FLAGS) -DSHELL_AS_APP -Idrivers -c drivers/pci_decode.c -o $@

SH_PC98PT_OBJ = $(SH_OBJDIR)/pc98pt_user.o
$(SH_PC98PT_OBJ): drivers/pc98pt.c drivers/pc98pt.h
	@mkdir -p $(SH_OBJDIR)
	$(CC) $(PROGRAM_FLAGS) -DSHELL_AS_APP -Idrivers -c drivers/pc98pt.c -o $@

# main.c だけが #include する .inc の明示依存 (レシピ無し = 上のパターン規則に
# 前提だけを足す)。$(SHELL_DEPS) の wildcard でも拾えるが、wildcard は
# Makefile 読み込み時の 1 度しか評価されないので、新しく足した .inc が
# 同じ make の中で見落とされない保証をここに置く (userland/system/*.elf の
# install_recover.inc / hsync_protect.inc と同じ書き方)。
userland/shell/main.o:    userland/shell/sh_exec.inc
$(SH_OBJDIR)/main.o:      userland/shell/sh_exec.inc

userland/sh.elf: sdk/link/app.ld $(CRT0_OBJ) $(SH_OBJ) $(SH_PCI_DECODE_OBJ) $(SH_PC98PT_OBJ) $(FILER_DRAW_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) $(SH_OBJ) $(SH_PCI_DECODE_OBJ) $(SH_PC98PT_OBJ) $(LGRP_BEG) $(FILER_DRAW_OBJ) -los32save $(LGRP_END) -lc -lgcc

sh: $(CRT0_OBJ) userland/sh.bin

clean-sh:
	rm -rf $(SH_OBJDIR)
	rm -f userland/sh.elf userland/sh.raw userland/sh.bin

.PHONY: sh clean-sh

# === gshell — GUI シェル (Rust、票 W1) ===
# shell.bin と同じシェル帯 (0x300000, app_sys.ld) に常駐する WM。userland/rust の
# ワークスペース外 (userland/gshell/、独立ワークスペース) にあるので
# DEFINE_RUST_PROGRAM は使わず個別に書く。カーネルが /bin/gshell.bin を
# シェル帯へ直接ロードする (config.h SYS_GSHELL_BIN、K4)。
GSHELL_DIR = userland/gshell
# RUST_TARGET_JSON はこの後 (Rust 節) で定義されるので、ここでは前提の
# 展開時に空にならないよう名前を直に書く。
GSHELL_LIB = $(GSHELL_DIR)/target/i686-os32-none/release/libgshell.a

$(GSHELL_LIB): FORCE $(RUST_KAPI_RS)
	cd $(GSHELL_DIR) && RUSTC_WRAPPER=$(CURDIR)/sdk/rustc_stamp.py cargo build --release

userland/gshell.elf: sdk/link/app_sys.ld $(CRT0_OBJ) $(GSHELL_LIB) $(GFX_OBJ) $(LIBCFG_OBJ)
	$(LD) -m elf_i386 -T sdk/link/app_sys.ld -nostdlib --nmagic --gc-sections --allow-multiple-definition \
		-L$(LIBDIR) -L$(CROSS_DIR)/i386-elf/lib -L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0 \
		-o $@ $(CRT0_OBJ) $(LGRP_BEG) $(GFX_OBJ) $(LIBCFG_OBJ) $(GSHELL_LIB) $(LGRP_END) -lc -lgcc

gshell: $(CRT0_OBJ) userland/gshell.bin

clean-gshell:
	cd $(GSHELL_DIR) && cargo clean 2>/dev/null || true
	rm -f userland/gshell.elf userland/gshell.raw userland/gshell.bin

.PHONY: gshell clean-gshell

# === Game (対戦スゴロクRPG) ===
# ゲームは game/Makefile が SDK 経由でビルドする。ここでは定義を持たない。
# ゲームは別リポジトリ ske-studio/os32-game (SDK 経由ビルド)。

# (SKK Module / LZSS Command は廃止済み)

# === 共有 C ソースのユーザー空間ビルド ===
# lib/ のいくつかはカーネルと外部プログラムの双方から使う。カーネル側の
# lib/%.o は KERNEL_CFLAGS でビルドされる (build/kernel.mk) ため、
# そのオブジェクトを外部プログラムにリンクすると 1 つの .o が 2 つの
# リンクドメインに跨ることになる。ユーザー空間用は _prog.o として
# PROGRAM_FLAGS で別途ビルドする。
lib/utf8_prog.o: lib/utf8.c lib/utf8.h lib/utf8_internal.h include/memmap.h include/endian_le.h
	$(CC) $(PROGRAM_FLAGS) -Ilib -c $< -o $@

# === LZ4 Command ===
lib/lz4_prog.o: lib/lz4.c lib/lz4.h
	$(CC) $(PROGRAM_FLAGS) -Ilib -c $< -o $@

userland/system/lz4.o: userland/system/lz4.c
	$(CC) $(PROGRAM_FLAGS) -Ilib -c $< -o $@

userland/system/lz4.elf: sdk/link/app.ld $(CRT0_OBJ) userland/system/lz4.o lib/lz4_prog.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/system/lz4.o lib/lz4_prog.o -lc -lgcc

lz4_cmd: $(CRT0_OBJ) userland/system/lz4.bin

# === CD Installer (cdinst) ===
userland/lib/rt/pkg.o: userland/lib/rt/pkg.c userland/lib/rt/pkg.h
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/system/cdinst.o: userland/system/cdinst.c userland/lib/rt/pkg.h userland/lib/rt/dbgserial.h
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

# hd0 の規則は install と共有 (userland/system/inst_hdd.c / inst_disk.c)。区画表
# (drivers/pc98pt.c)、幾何と計画 (userland/shell/hdprep_plan.c)、ext2 の配置
# (fs/ext2_layout.c) はカーネル・hdprep と同じソースを組む — **写さない**
# (票 TASK_HDD_INSTALL 段 2-9)。
INST_HDRS = userland/system/inst_hdd.h userland/system/inst_disk.h \
            userland/shell/hdprep_plan.h drivers/pc98pt.h fs/ext2_layout.h $(SDK_KAPI_HDR)
userland/system/inst_disk.o: userland/system/inst_disk.c $(INST_HDRS)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@
userland/system/inst_hdd.o: userland/system/inst_hdd.c $(INST_HDRS)
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@
userland/system/ext2_layout_user.o: fs/ext2_layout.c fs/ext2_layout.h
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@
userland/system/hdprep_plan_inst.o: userland/shell/hdprep_plan.c userland/shell/hdprep_plan.h drivers/pc98pt.h
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@
userland/system/pc98pt_inst.o: drivers/pc98pt.c drivers/pc98pt.h
	$(CC) $(PROGRAM_FLAGS) -Idrivers -c $< -o $@
INST_OBJ = userland/system/inst_hdd.o userland/system/inst_disk.o \
           userland/system/hdprep_plan_inst.o userland/system/pc98pt_inst.o \
           userland/system/ext2_layout_user.o

userland/system/cdinst.o: $(INST_HDRS)
userland/system/cdinst.elf: sdk/link/app.ld $(CRT0_OBJ) userland/system/cdinst.o userland/lib/rt/pkg.o $(DBG_OBJ) $(INST_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/system/cdinst.o userland/lib/rt/pkg.o $(DBG_OBJ) $(INST_OBJ) -lc -lgcc

cdinst: $(CRT0_OBJ) userland/system/cdinst.bin

# === Bench Module ===
BENCH_SRC = $(wildcard userland/tests/bench/*.c)
BENCH_OBJ = $(BENCH_SRC:.c=.o)

userland/tests/bench/%.o: userland/tests/bench/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -c $< -o $@

userland/tests/bench.elf: sdk/link/app.ld $(CRT0_OBJ) $(BENCH_OBJ) $(GFX_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) $(BENCH_OBJ) $(LGRP_BEG) $(GFX_OBJ) $(LGRP_END) -lc -lgcc

bench: $(CRT0_OBJ) userland/tests/bench.bin

# === Bench Scale2x Module ===
BENCH_S2X_SRC = userland/tests/bench_scale2x/main.c
BENCH_S2X_OBJ = $(BENCH_S2X_SRC:.c=.o)

userland/tests/bench_scale2x/%.o: userland/tests/bench_scale2x/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -c $< -o $@

userland/tests/bench_scale2x.elf: sdk/link/app.ld $(CRT0_OBJ) $(BENCH_S2X_OBJ) $(GFX_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) $(BENCH_S2X_OBJ) $(LGRP_BEG) $(GFX_OBJ) $(LGRP_END) -lc -lgcc

bench_scale2x: $(CRT0_OBJ) userland/tests/bench_scale2x.bin

# --- faultprobe (リング3 フォールト注入テスト, M3) ---
FAULTPROBE_SRC = userland/tests/faultprobe/main.c
FAULTPROBE_OBJ = $(FAULTPROBE_SRC:.c=.o)

userland/tests/faultprobe/%.o: userland/tests/faultprobe/%.c
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/tests/faultprobe.elf: sdk/link/app.ld $(CRT0_OBJ) $(FAULTPROBE_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) $(FAULTPROBE_OBJ) $(LGRP_BEG) $(LGRP_END) -lc -lgcc

faultprobe: $(CRT0_OBJ) userland/tests/faultprobe.bin

# --- ring3_hello / ring3_fault / ring3_guard (CPL=3 検証用) ---
# 3 本ともグラフィック VRAM 0xA8000 に機械可読マーカーを直接書くが、
# --cui-only (票 T8-2) は**立てない** (PM 判断 2026-09-12): ring3_fault は
# GUI 中の fault 隔離 (K5b 受入 G5) の観測手段で、GUI から起動できる必要がある。
# マーカーは数バイトで WM の画面を実用上壊さない。
# --- ring3_hello (CPL=3 検証用最小プログラム, v2 M1) ---
# crt0 を link しない自己完結バイナリ (独自 _start)。標準 crt0 は
# kapi->sys_exit() 等カーネル関数ポインタを呼ぶが、M2 トランポリン前は CPL=3
# から呼べないため。mkos32x に --ring3 を付け OS32X_FLAG_RING3 を立てる。
# KAPI 不使用なので --api は最小でよい。explicit ルールなので generic の
# %.elf / %.bin / %.raw パターンより優先される (crt0 リンクを回避)。
# ヘッダ v3 (票 TASK_KAPI_DATA_FIELDS): crt0 を付けない 3 本 (ring3_hello /
# ring3_fault / ring3_guard) は KAPI データ欄の配置の刻印を持たないので、
# ソースに OS32_KAPI_LAYOUT_STAMP(); を明示して置く (mkos32x は刻印の無い ELF を
# 断る)。--api 39 は mkos32x が v3 の最低版 63 へ引き上げる (旧カーネルでは
# 走らない = v3 の照合を持たないカーネルに載せない)。hello_r3 は
# crt0 をリンクした ELF の流用なので crt0 の刻印をそのまま持つ。
userland/tests/ring3_hello.o: userland/tests/ring3_hello.c
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/tests/ring3_hello.elf: userland/tests/ring3_hello.o sdk/link/app.ld
	$(LD) -m elf_i386 -T sdk/link/app.ld -nostdlib --nmagic --gc-sections -o $@ userland/tests/ring3_hello.o

userland/tests/ring3_hello.bin: userland/tests/ring3_hello.elf
	$(OBJCOPY) -O binary $< userland/tests/ring3_hello.raw
	python3 sdk/mkos32x.py userland/tests/ring3_hello.raw $@ --elf $< --api 39 --ring3
	@rm -f userland/tests/ring3_hello.raw

ring3_hello: userland/tests/ring3_hello.bin
.PHONY: ring3_hello

# --- ring3_fault (CPL=3 フォールト kill 検証, v2 M1e) ---
# ring3_hello と同形の crt0 非依存・--ring3 バイナリ。わざとカーネル帯域へ
# 書き込み #PF させ、M1e の kill + fault_kill_count を検証する。
userland/tests/ring3_fault.o: userland/tests/ring3_fault.c
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/tests/ring3_fault.elf: userland/tests/ring3_fault.o sdk/link/app.ld
	$(LD) -m elf_i386 -T sdk/link/app.ld -nostdlib --nmagic --gc-sections -o $@ userland/tests/ring3_fault.o

userland/tests/ring3_fault.bin: userland/tests/ring3_fault.elf
	$(OBJCOPY) -O binary $< userland/tests/ring3_fault.raw
	python3 sdk/mkos32x.py userland/tests/ring3_fault.raw $@ --elf $< --api 39 --ring3
	@rm -f userland/tests/ring3_fault.raw

ring3_fault: userland/tests/ring3_fault.bin
.PHONY: ring3_fault

# --- ring3_guard (M3 ハードニング: heap/stack ガードページ検証) ---
userland/tests/ring3_guard.o: userland/tests/ring3_guard.c
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/tests/ring3_guard.elf: userland/tests/ring3_guard.o sdk/link/app.ld
	$(LD) -m elf_i386 -T sdk/link/app.ld -nostdlib --nmagic --gc-sections -o $@ userland/tests/ring3_guard.o

userland/tests/ring3_guard.bin: userland/tests/ring3_guard.elf
	$(OBJCOPY) -O binary $< userland/tests/ring3_guard.raw
	python3 sdk/mkos32x.py userland/tests/ring3_guard.raw $@ --elf $< --api 39
	@rm -f userland/tests/ring3_guard.raw

ring3_guard: userland/tests/ring3_guard.bin
.PHONY: ring3_guard

# --- hello_r3 (M2 KAPI トランポリン検証: 既存 hello.c をソース無変更で CPL=3) ---
# hello.elf (crt0 リンク済み, 通常ビルド) をそのまま使い、--ring3 を付けた
# 別名 .bin にする。api->kprintf(%d/%s) / api->magic / api->version /
# crt0 の sys_exit 往復を CPL=3 で網羅する (T1-T5)。ソースは一切変えない。
userland/tests/hello_r3.bin: userland/tests/hello.elf
	$(OBJCOPY) -O binary $< userland/tests/hello_r3.raw
	python3 sdk/mkos32x.py userland/tests/hello_r3.raw $@ --elf $< --api 39 --ring3
	@rm -f userland/tests/hello_r3.raw

hello_r3: userland/tests/hello_r3.bin
.PHONY: hello_r3


# ---------------------------------------------------------------------------
# DEFINE_TEST — テストプログラム定義テンプレート
# $(1) = テスト名 (tests/ 以下のベース名)
# $(2) = リンク対象 OBJ リスト
# $(3) = 追加コンパイルフラグ
# ---------------------------------------------------------------------------
# $(4) = テストの所在ディレクトリ (userland/tests または game/tests)
define DEFINE_TEST
$(4)/$(1).o: $(4)/$(1).c
	$$(CC) $$(PROGRAM_FLAGS) $(3) -c $$< -o $$@

$(4)/$(1).elf: sdk/link/app.ld $$(CRT0_OBJ) $(4)/$(1).o $(2)
	$$(LD) $$(PROGRAM_LDFLAGS) -o $$@ $$(CRT0_OBJ) $(4)/$(1).o $$(LGRP_BEG) $(2) $$(LGRP_END) -lc -lgcc

$(1): $$(CRT0_OBJ) $(4)/$(1).bin

.PHONY: $(1)
endef

# --- テストプログラム登録 ---

$(eval $(call DEFINE_TEST,gfx200_test,$$(GFX_OBJ),$$(INC_libos32gfx),userland/tests))
$(eval $(call DEFINE_TEST,gfx_demo200,$$(GFX_OBJ),$$(INC_libos32gfx),userland/tests))
$(eval $(call DEFINE_TEST,blit_test,$$(GFX_OBJ),$$(INC_libos32gfx),userland/tests))
$(eval $(call DEFINE_TEST,blit_test2,$$(GFX_OBJ),$$(INC_libos32gfx),userland/tests))
$(eval $(call DEFINE_TEST,rotate_test,$$(GFX_OBJ),$$(INC_libos32gfx),userland/tests))
$(eval $(call DEFINE_TEST,demo_tile,$$(TILEMAP_OBJ) $$(GFX_OBJ) $$(LIBASSET_OBJ),$$(INC_libos32tilemap),userland/tests))
$(eval $(call DEFINE_TEST,tile_bench,$$(TILEMAP_OBJ) $$(GFX_OBJ) $$(LIBASSET_OBJ),$$(INC_libos32tilemap),userland/tests))
$(eval $(call DEFINE_TEST,db_test,$$(LIBOS32DB_OBJ),$$(INC_libos32db),userland/tests))
$(eval $(call DEFINE_TEST,dbq,$$(LIBOS32DB_OBJ),$$(INC_libos32db),userland/tests))
$(eval $(call DEFINE_TEST,e2test,,,userland/tests))
$(eval $(call DEFINE_TEST,math_test,$$(LIBMATH_OBJ),$$(INC_libos32math),userland/tests))
$(eval $(call DEFINE_TEST,input_test,$$(LIBINPUT_OBJ) $$(LIBMATH_OBJ),$$(INC_libos32input),userland/tests))
$(eval $(call DEFINE_TEST,asset_test,$$(LIBASSET_OBJ),$$(INC_libos32asset),userland/tests))
$(eval $(call DEFINE_TEST,asset_demo,$$(LIBASSET_OBJ),$$(INC_libos32asset),userland/tests))
$(eval $(call DEFINE_TEST,ecs_test,$$(LIBECS_OBJ) $$(LIBMATH_OBJ),$$(INC_libos32ecs),userland/tests))
$(eval $(call DEFINE_TEST,save_test,$$(LIBSAVE_OBJ),$$(INC_libos32save),userland/tests))
$(eval $(call DEFINE_TEST,mgx_test,$$(LIBMGX_OBJ),$$(INC_libos32mgx),userland/tests))
# kbd_echo — KAPI だけで動く CUI キー入力エコー (票 K7 の I1/I2/I3 観測手段)
$(eval $(call DEFINE_TEST,kbd_echo,,,userland/tests))

# === 標準アプリ ===
# アプリは apps/Makefile が SDK 経由でビルドする。ここでは定義を持たない。
# 標準アプリは別リポジトリ ske-studio/os32-apps (SDK 経由ビルド)。

# libos32gfx/ui.o (gfx_demo が参照)
userland/lib/gfx/ui.o: userland/lib/gfx/ui.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -c $< -o $@

# === SQLite Standalone Test ===
SQLITE_SA_DIR = userland/tests/sqlite_standalone
SQLITE_SA_CFLAGS = $(C_STD_SQLITE) -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -mno-red-zone -O1 -fcommon -Wno-long-long -w -I. -Iinclude $(SDK_INC) -Iuserland/lib -Ilib/sqlite3 -include lib/sqlite3/os32_sqlite_config.h -I$(CROSS_DIR)/i386-elf/include
SQLITE_SA_CFLAGS += -include sdk/include/os32/os32_unit_stamp.h

$(SQLITE_SA_DIR)/sqlite3_user.o: lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_config.h
	$(CC) $(SQLITE_SA_CFLAGS) -c $< -o $@

$(SQLITE_SA_DIR)/sqlite_user_vfs.o: $(SQLITE_SA_DIR)/sqlite_user_vfs.c lib/sqlite3/os32_sqlite_config.h
	$(CC) $(SQLITE_SA_CFLAGS) -c $< -o $@

$(SQLITE_SA_DIR)/sqlite_standalone.o: $(SQLITE_SA_DIR)/sqlite_standalone.c lib/sqlite3/os32_sqlite_config.h
	$(CC) $(SQLITE_SA_CFLAGS) -c $< -o $@

SQLITE_SA_OBJ = $(CRT0_OBJ) $(DBG_OBJ) \
	$(SQLITE_SA_DIR)/sqlite_standalone.o \
	$(SQLITE_SA_DIR)/sqlite_user_vfs.o \
	$(SQLITE_SA_DIR)/sqlite3_user.o

$(SQLITE_SA_DIR)/sqlite_standalone.elf: sdk/link/app.ld $(SQLITE_SA_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(SQLITE_SA_OBJ) -lc -lgcc

sqlite_standalone: $(CRT0_OBJ) $(SQLITE_SA_DIR)/sqlite_standalone.bin

# === ベースプログラム パターンルール ===
userland/cmds/%.elf: userland/cmds/%.c sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -c $< -o userland/cmds/$*.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/$*.o -lc -lgcc

# cfg — 設定レジストリの CUI (libos32cfg を静的リンク、票 S2-C)。既定の
# cmds/%.elf パターンはライブラリを引けないので明示規則。
userland/cmds/cfg.o: userland/cmds/cfg.c userland/lib/cfg/libos32cfg.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32cfg) -c $< -o $@

userland/cmds/cfg.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/cfg.o $(LIBCFG_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/cfg.o \
	      $(LGRP_BEG) $(LIBCFG_OBJ) $(LGRP_END) -lc -lgcc

# cfg_bench — 設定レジストリの計測 (票 S5-C、libos32cfg を静的リンク)。明示規則は
# 既定の tests/%.elf パターンに優先する。
userland/tests/cfg_bench.o: userland/tests/cfg_bench.c userland/lib/cfg/libos32cfg.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32cfg) -c $< -o $@

userland/tests/cfg_bench.elf: sdk/link/app.ld $(CRT0_OBJ) userland/tests/cfg_bench.o $(LIBCFG_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/tests/cfg_bench.o \
	      $(LGRP_BEG) $(LIBCFG_OBJ) $(LGRP_END) -lc -lgcc

# kstr_bench — kstring のアセンブリ版と C 版を実機で測る
# (票 docs/archive/portability/TASK_KSTRING_BENCH.md)。
#
# **出荷するソースそのもの**を測る。写しは作らない。lib/kstring_asm.asm と
# lib/kstring_c.c は 13 本すべてが同名なので 1 つの実行ファイルに入らない。
# tools/tests/test_kstring_c.py と同じ手で objcopy --redefine-syms に
# 接頭辞を付けさせ、アセンブリ版を a_*、C 版を c_* にして同居させる
# (libc の memcpy / strlen 等とも衝突しなくなる副産物つき)。
#
# 名前の表はここが管理元。lib/kstring_asm.asm の global が増減したら
# ここと userland/tests/kstr_bench.c の kb_build_table を直す
# (tools/tests/test_kstr_bench.py が 3 者のずれを検出する)。
KSTR_BENCH_FUNCS = kmemcpy memcpy kmemset memset kstrlen strlen kstrcmp \
                   strcmp kstrncmp strncmp kstrcpy kstrncpy memcmp

userland/tests/kstr_asm_a.o: lib/kstring_asm.asm
	$(AS) -p sdk/crt/generations.inc -f elf32 $< -o userland/tests/kstr_asm_raw.o
	@for f in $(KSTR_BENCH_FUNCS); do echo "$$f a_$$f"; done > userland/tests/kstr_ren_a.txt
	$(OBJCOPY) --redefine-syms=userland/tests/kstr_ren_a.txt userland/tests/kstr_asm_raw.o $@
	@rm -f userland/tests/kstr_asm_raw.o userland/tests/kstr_ren_a.txt

userland/tests/kstr_c_c.o: lib/kstring_c.c lib/kstring.h include/types.h
	$(CC) $(PROGRAM_FLAGS) -Ilib -c $< -o userland/tests/kstr_c_raw.o
	@for f in $(KSTR_BENCH_FUNCS); do echo "$$f c_$$f"; done > userland/tests/kstr_ren_c.txt
	$(OBJCOPY) --redefine-syms=userland/tests/kstr_ren_c.txt userland/tests/kstr_c_raw.o $@
	@rm -f userland/tests/kstr_asm_raw.o userland/tests/kstr_c_raw.o userland/tests/kstr_c_raw.d userland/tests/kstr_ren_c.txt

userland/tests/kstr_bench.o: userland/tests/kstr_bench.c
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/tests/kstr_bench.elf: sdk/link/app.ld $(CRT0_OBJ) userland/tests/kstr_bench.o \
                               userland/tests/kstr_asm_a.o userland/tests/kstr_c_c.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/tests/kstr_bench.o \
	      userland/tests/kstr_asm_a.o userland/tests/kstr_c_c.o -lc -lgcc

kstr_bench: $(CRT0_OBJ) userland/tests/kstr_bench.bin

# tar — ustar の作成 / 展開 / 一覧 (票 S6)。ustar の読み書きは vendor した
# lib/microtar (rxi、MIT)。lib/lz4_prog.o と同じく、カーネル側とは別に
# PROGRAM_FLAGS でビルドした _prog.o を明示規則でリンクする
# (cmds/%.elf パターンはライブラリを引けない)。
lib/microtar/microtar_prog.o: lib/microtar/microtar.c lib/microtar/microtar.h
	$(CC) $(PROGRAM_FLAGS) -DMTAR_NO_STDIO -Ilib/microtar -c $< -o $@

userland/cmds/tar.o: userland/cmds/tar.c lib/microtar/microtar.h
	$(CC) $(PROGRAM_FLAGS) -DMTAR_NO_STDIO -Ilib/microtar -c $< -o $@

userland/cmds/tar.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/tar.o lib/microtar/microtar_prog.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/tar.o \
	      lib/microtar/microtar_prog.o -lc -lgcc

# Host Services のコマンド (票 N3) — libos32host を静的リンク。既定の
# cmds/%.elf パターンはライブラリを引けないので明示規則 (cfg.elf と同じ形)。
userland/cmds/wget.o: userland/cmds/wget.c userland/lib/host/libos32host.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32host) -c $< -o $@
userland/cmds/wget.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/wget.o $(LIBHOST_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/wget.o \
	      $(LGRP_BEG) $(LIBHOST_OBJ) $(LGRP_END) -lc -lgcc

userland/cmds/lpr.o: userland/cmds/lpr.c userland/lib/host/libos32host.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32host) -c $< -o $@
userland/cmds/lpr.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/lpr.o $(LIBHOST_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/lpr.o \
	      $(LGRP_BEG) $(LIBHOST_OBJ) $(LGRP_END) -lc -lgcc

userland/cmds/hclip.o: userland/cmds/hclip.c userland/lib/host/libos32host.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32host) -c $< -o $@
userland/cmds/hclip.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/hclip.o $(LIBHOST_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/hclip.o \
	      $(LGRP_BEG) $(LIBHOST_OBJ) $(LGRP_END) -lc -lgcc

userland/cmds/hdate.o: userland/cmds/hdate.c userland/lib/host/libos32host.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32host) -c $< -o $@
userland/cmds/hdate.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/hdate.o $(LIBHOST_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/hdate.o \
	      $(LGRP_BEG) $(LIBHOST_OBJ) $(LGRP_END) -lc -lgcc

userland/tests/%.elf: userland/tests/%.c sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -c $< -o userland/tests/$*.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/tests/$*.o -lc -lgcc

userland/tests/bench/%.elf: userland/tests/bench/%.c sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -c $< -o userland/tests/bench/$*.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/tests/bench/$*.o -lc -lgcc

userland/system/%.elf: userland/system/%.c sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -c $< -o userland/system/$*.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/system/$*.o -lc -lgcc

# .inc を #include する system プログラムの明示依存 (レシピ無し = 上のパターン規則に前提だけ足す)。
# 2026-09-14: install_recover.inc を直しても install.bin が再ビルドされず古いバイナリを配備した
# (userland の .d は Makefile 末尾の -include の対象外)。
userland/system/install.elf: userland/system/install_recover.inc
# install は hd0 の手順を cdinst と共有する (上の INST_OBJ)。パターン規則は
# 自分の .o しか繋がないので、install だけ明示の規則で置き換える。
userland/system/install.elf: userland/system/install.c $(INST_HDRS) $(INST_OBJ) sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -c $< -o userland/system/install.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/system/install.o $(INST_OBJ) -lc -lgcc
userland/system/hsync.elf: userland/system/hsync_protect.inc
userland/system/hsync.elf: lib/crc32_core.inc

# === ELF → RAW → OS32X BIN 変換 ===
# ユーザーランドぶん。ゲームは game/Makefile が自前で持つ。
# build/app.conf のキーはリポジトリルートからの拡張子なしパス
# (例: userland/cmds/wc)。キーが実在するターゲットと
# 一致しているかは make check-app-conf で検査できる。
# 列: 名前 APIバージョン ヒープサイズ [gfx|cui|launcher]
#   4 列目 gfx = 全画面 GFX を使う宣言 (OS32X_FLAG_GFX、mkos32x --gfx)。
#   省略 = 無し。gfx_init / gfx_init_200 を呼ぶプログラムに立てる (票 T8 D1a)。
#   4 列目 cui = CUI 専用の宣言 (OS32X_FLAG_CUI_ONLY、mkos32x --cui-only)。
#   KAPI の向こうで画面と BIOS を丸ごと持っていくもの (v86 / VDM) に立てる。
#   GUI からの exec_start はこれを OS32_ERR_INVAL で断る (票 T8-2)。
#   4 列目 launcher = 起動要求者の宣言 (OS32X_FLAG_LAUNCHER、mkos32x --launcher)。
#   launch_req (KAPI v49) を呼ぶもの (端末 / sh) に立てる。カーネルはこの
#   宣言の無い CPL=3 からの launch_req を断る (票 T9 D3、D1a)。
#   いずれも立て忘れは make check-manifests が検出する。
userland/%.raw: userland/%.elf
	$(OBJCOPY) -O binary $< $@

userland/%.bin: userland/%.raw userland/%.elf
	@_api=$$(awk '$$1 == "userland/$*" { print $$2 }' build/app.conf); \
	_heap=$$(awk '$$1 == "userland/$*" { print $$3 }' build/app.conf); \
	_decl=$$(awk '$$1 == "userland/$*" { print $$4 }' build/app.conf); \
	_api=$${_api:-7}; \
	_heap=$${_heap:-0}; \
	_opts=""; \
	if [ "$$_heap" != "0" ]; then _opts="$$_opts --heap $$_heap"; fi; \
	if [ "$$_decl" = "gfx" ]; then _opts="$$_opts --gfx"; fi; \
	if [ "$$_decl" = "cui" ]; then _opts="$$_opts --cui-only"; fi; \
	if [ "$$_decl" = "launcher" ]; then _opts="$$_opts --launcher"; fi; \
	python3 sdk/mkos32x.py $< $@ --elf userland/$*.elf --api $$_api $$_opts

# === ヘルパーツール ===
unicode_bin:
	@if [ ! -f tools/gen_unicode ]; then gcc tools/gen_unicode.c -I. -Iinclude -O2 -o tools/gen_unicode; fi
	@if [ ! -f $(BUILD_OUT)/unicode.bin ]; then ./tools/gen_unicode && mv unicode.bin $(BUILD_OUT)/unicode.bin; fi

fep_dic:
	@if [ ! -f assets/fep.db ]; then python3 tools/fep_to_sqlite.py; fi

# ============================================================================
# Rust プログラム ビルドルール
# ============================================================================
RUST_PROGRAMS_DIR = userland/rust
RUST_TARGET_JSON = i686-os32-none
RUST_TARGET_DIR = $(RUST_PROGRAMS_DIR)/target/$(RUST_TARGET_JSON)/release
RUST_KAPI_RS = sdk/rust/os32api/src/kapi_generated.rs

# Rustバインディング自動再生成 (kapi.json変更時)
$(RUST_KAPI_RS): sdk/kapi.json sdk/kapi_rust_gen.py
	python3 sdk/kapi_rust_gen.py

# ---------------------------------------------------------------------------
# DEFINE_RUST_PROGRAM — Rustプログラム定義テンプレート
# $(1) = Rustクレート名 (Cargoワークスペースメンバー名)
# $(2) = 出力先ディレクトリ (例: userland/tests)
# $(3) = 追加リンクOBJ (例: $(GFX_OBJ))
# ---------------------------------------------------------------------------
define DEFINE_RUST_PROGRAM
$(RUST_TARGET_DIR)/lib$(1).a: FORCE $(RUST_KAPI_RS)
	cd $(RUST_PROGRAMS_DIR) && RUSTC_WRAPPER=$(CURDIR)/sdk/rustc_stamp.py cargo build --release -p $(1)

$(2)/$(1).elf: sdk/link/app.ld $$(CRT0_OBJ) $(RUST_TARGET_DIR)/lib$(1).a $(3)
	$$(LD) $$(PROGRAM_LDFLAGS) --allow-multiple-definition \
		-o $$@ $$(CRT0_OBJ) \
		$$(LGRP_BEG) $(3) $(RUST_TARGET_DIR)/lib$(1).a $$(LGRP_END) \
		-lc -lgcc

$(1)_rust: $$(CRT0_OBJ) $(2)/$(1).bin

.PHONY: $(1)_rust
endef

# --- Rustプログラム登録 ---
$(eval $(call DEFINE_RUST_PROGRAM,hello_gfx,userland/tests,$$(GFX_OBJ)))
$(eval $(call DEFINE_RUST_PROGRAM,alloc_demo,userland/tests,))
$(eval $(call DEFINE_RUST_PROGRAM,math_test_rs,userland/tests,))
$(eval $(call DEFINE_RUST_PROGRAM,font_test,userland/tests,$$(GFX_OBJ)))
# gshell 配下の GUI アプリは libos32gui_stub をリンクし、描画とウィジェットの本体は
# 共有ライブラリ /sys/lib/libos32gui.shlib (下) にある。libos32gfx もライブラリ側に
# 入っているので $(GFX_OBJ) は要らない (C3)。gdi_test だけは libos32gfx_init を
# 直接呼ぶ単独 GFX プログラムなので従来どおり。
$(eval $(call DEFINE_RUST_PROGRAM,gui_demo,userland/tests,))
# v1.2 の client API 試験 (C4): MessageBox/File/Input の modal_result、session_launch、icon16。stub のみ
$(eval $(call DEFINE_RUST_PROGRAM,v12_api_test,userland/tests,))
# T5a: 固定テキスト表示試験。Unicode変換だけをユーザー空間Cからリンク。
$(eval $(call DEFINE_RUST_PROGRAM,t5a_display,userland/tests,lib/utf8_prog.o))
programs: t5a_display_rust
# v1.2 File Manager (C5): Win3.1 風 2 ペイン、SESSION_REQUEST で起動依頼。stub のみ
$(eval $(call DEFINE_RUST_PROGRAM,filer,userland/system,))
# 票 TASK_EDIT_GUI: テキストエディタの GUI 版 (v1.4 の最後の受入試験)。
# CUI 版 apps/edit とは別物なので名前を分ける (/usr/bin/edit_gui.bin)。
$(eval $(call DEFINE_RUST_PROGRAM,edit_gui,userland/system,))
# ROADMAP v1.4 About dialog (/usr/bin/about.bin)。Start → Programs の about.bin から起動する。
$(eval $(call DEFINE_RUST_PROGRAM,about,userland/system,))
$(eval $(call DEFINE_RUST_PROGRAM,gdi_test,userland/tests,$$(GFX_OBJ)))
$(eval $(call DEFINE_RUST_PROGRAM,lease_test,userland/tests,))
$(eval $(call DEFINE_RUST_PROGRAM,gui_bench,userland/tests,))

# === libos32gui.shlib — 共有ライブラリ (0x400000 常駐、票 C3 / K3) ===
# 先頭 4KB がジャンプ表 (OS32ShlibHeader)。crt0 は付けない (入口は表であって
# _start ではない)。tools/mkshlib.py が ELF のセクション情報からヘッダの
# text_pages / data_vaddr / data_pages を焼き、OS32X_FLAG_SHLIB 付きの OS32X にする。
# 番号表 (shlib.rs と stub.rs) の突き合わせは make check-shlib。
SHLIB_GUI_LIB = $(RUST_TARGET_DIR)/liblibos32gui.a

$(SHLIB_GUI_LIB): FORCE $(RUST_KAPI_RS)
	cd $(RUST_PROGRAMS_DIR) && RUSTC_WRAPPER=$(CURDIR)/sdk/rustc_stamp.py cargo build --release -p libos32gui

# libos32host.a (票 N4) も静的リンク: 表 105..=110 の host_* が呼ぶ。`kapi` は
# libos32cfg (cfg_backend.c) と共用で SHLIB_GUI_LIB (cfgro.rs) が供給する
# (--allow-multiple-definition 済み。nm で kapi 定義は 1 本を確認)。
# メモリ: libos32host の bss 16KB (print_stream の g_stream_buf) で shlib の
# per-app .data/.bss が 4 → 8 ページ (data_pages=8、K3 がアプリごとに複製)。
# GUI アプリ 1 本あたり +16KB。共有 .text は host_* ぶんだけ増える。
userland/libos32gui.elf: sdk/link/shlib.ld $(SHLIB_GUI_LIB) $(GFX_OBJ) $(LIBCFG_OBJ) $(LIBHOST_OBJ)
	$(LD) -m elf_i386 -T sdk/link/shlib.ld -nostdlib --nmagic --gc-sections \
		--allow-multiple-definition -L$(LIBDIR) -L$(CROSS_DIR)/i386-elf/lib \
		-L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0 -o $@ \
		$(LGRP_BEG) $(GFX_OBJ) $(LIBCFG_OBJ) $(LIBHOST_OBJ) $(SHLIB_GUI_LIB) $(LGRP_END) -lc -lgcc

userland/libos32gui.raw: userland/libos32gui.elf
	$(OBJCOPY) -O binary $< $@

userland/libos32gui.shlib: userland/libos32gui.raw userland/libos32gui.elf tools/mkshlib.py
	python3 tools/mkshlib.py $< $@ --elf userland/libos32gui.elf --api 51

shlib: userland/libos32gui.shlib

.PHONY: shlib

# Rustクリーン
clean-rust:
	cd $(RUST_PROGRAMS_DIR) && cargo clean 2>/dev/null || true

FORCE:
.PHONY: clean-rust FORCE

# === プログラム集約ターゲット ===
# programs: に足し忘れたターゲットは make all でビルドされないまま
# tools/deploy.yaml が古いバイナリを NHD に残す (deploy.yaml 冒頭の警告を参照)。
# プログラムを追加したらこの一覧にも必ず足すこと。
programs_base: $(CRT0_OBJ) $(BASE_PROGRAMS_BIN)

programs: libs $(DBG_OBJ) programs_base sh bench cdinst lz4_cmd bench_scale2x faultprobe ring3_hello ring3_fault ring3_guard hello_r3 gfx200_test gfx_demo200 blit_test blit_test2 demo_tile tile_bench rotate_test db_test dbq e2test sqlite_standalone math_test input_test kbd_echo asset_test asset_demo ecs_test save_test mgx_test kstr_bench hello_gfx_rust alloc_demo_rust math_test_rs_rust font_test_rust gui_demo_rust gdi_test_rust lease_test_rust gui_bench_rust v12_api_test_rust filer_rust edit_gui_rust about_rust gshell shlib

# === KAPI ヘッダ依存 ===
userland/%.o: $(SDK_KAPI_HDR)
$(shell find userland -name '*.o' 2>/dev/null): $(SDK_KAPI_HDR)

# === プログラムクリーン ===
clean-programs: clean-rust clean-gshell
	rm -f userland/cmds/*.o userland/cmds/*.elf userland/cmds/*.raw userland/cmds/*.bin
	rm -f userland/tests/*.o userland/tests/*.elf userland/tests/*.raw userland/tests/*.bin
	rm -f userland/tests/kstr_ren_a.txt userland/tests/kstr_ren_c.txt userland/tests/kstr_c_raw.d
	rm -f userland/tests/bench/*.o userland/tests/bench/*.elf userland/tests/bench/*.raw userland/tests/bench/*.bin
	rm -f userland/tests/bench_scale2x/*.o userland/tests/bench_scale2x/*.elf userland/tests/bench_scale2x/*.raw userland/tests/bench_scale2x/*.bin
	rm -f userland/system/*.o userland/system/*.elf userland/system/*.raw userland/system/*.bin
	rm -f sdk/crt/*.o
	rm -f userland/shell/*.o
	rm -rf userland/shell/sh_obj
	rm -f userland/sh.elf userland/sh.raw userland/sh.bin

	rm -f userland/lib/rt/*.o
	rm -f userland/tests/sqlite_standalone/*.o userland/tests/sqlite_standalone/*.elf userland/tests/sqlite_standalone/*.raw userland/tests/sqlite_standalone/*.bin
	rm -f lib/lz4_prog.o lib/utf8_prog.o
	rm -f lib/zlib/*.o
	rm -f lib/microtar/*.o
	rm -f $(BUILD_OUT)/unicode.bin tools/gen_unicode

.PHONY: programs programs_base game sh lz4_cmd cdinst bench bench_scale2x faultprobe
.PHONY: gfx200_test gfx_demo200 blit_test blit_test2 rotate_test
.PHONY: demo_tile tile_bench db_test e2test sqlite_standalone math_test
.PHONY: input_test kbd_echo kstr_bench
.PHONY: asset_test asset_demo ecs_test text_demo
.PHONY: inv_test
.PHONY: save_test

.PHONY: unicode_bin fep_dic
.PHONY: clean-programs

# T2h h2: same guest, default 256KiB and explicit 512KiB; no public format change.
H2_STACK512_BYTES := 524288
userland/tests/h2_stack.elf: userland/tests/h2_stack_probe.h userland/tests/h2_stack_probe.inc include/memmap.h sdk/include/os32/os32_gui_shared.h
userland/tests/h2_stack512.elf: userland/tests/h2_stack.c userland/tests/h2_stack_probe.h userland/tests/h2_stack_probe.inc include/memmap.h sdk/include/os32/os32_gui_shared.h sdk/link/app.ld $(CRT0_OBJ)
	$(CC) $(PROGRAM_FLAGS) -DH2_STACK_BYTES=$(H2_STACK512_BYTES) -c $< -o userland/tests/h2_stack512.o
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/tests/h2_stack512.o -lc -lgcc
userland/tests/h2_stack512.bin: userland/tests/h2_stack512.raw userland/tests/h2_stack512.elf sdk/mkos32x.py
	python3 sdk/mkos32x.py $< $@ --elf userland/tests/h2_stack512.elf --stack $(H2_STACK512_BYTES)

# Explicit opt-in only. .fixture files never enter deploy manifests/packages.
.PHONY: h2-fixtures
h2-fixtures: userland/tests/h2_stack.bin userland/shell.bin userland/libos32gui.shlib userland/tests/gui_demo.bin
	python3 tools/gen_h2_fixtures.py --out $(BUILD_OUT)/h2-isolated

# T2h/h3 shared fixture code (the generic rule does not include userland .d).
userland/tests/h3a.o: userland/tests/h3a.c userland/tests/h3/fixture.inc userland/tests/h3/state.inc userland/tests/h3/protocol.h
userland/tests/h3b.o: userland/tests/h3b.c userland/tests/h3/fixture.inc userland/tests/h3/state.inc userland/tests/h3/protocol.h
