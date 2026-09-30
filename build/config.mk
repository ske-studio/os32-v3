# ============================================================================
#  config.mk — ツールチェーン、フラグ、パス定義
# ============================================================================

# ビルド成果物出力ディレクトリ (kernel.bin / sqlite.bin / vmkernel.lz4 /
# unicode.bin / kernel.elf / kernel.map をリポジトリルートから分離する)
BUILD_OUT = build/out
$(shell mkdir -p $(BUILD_OUT))

# ユーザー空間ライブラリのアーカイブ (.a) 出力先。
# 外部プログラムはここを -L で見る。将来 SDK を切り出すときは
# このディレクトリがそのまま sysroot の lib/ になる。
LIBDIR = $(BUILD_OUT)/lib
$(shell mkdir -p $(LIBDIR))

# 環境変数 (デプロイ先)
NP21W_DIR ?= /tmp/np21w

# クロスコンパイラパス
CROSS_DIR ?= /usr/local/cross

# Directories
PROJDIR = .

# === 移植の 2 本の軸 (CPU と機種) ===
# include/io.h は原始命令の **契約** だけを持ち、実装は固定名
# "arch_io.h" / "platform_io.h" を引く。どの実装が来るかは下の 2 つが
# 決める INC_COMMON の -I だけで決まるので、アーキテクチャや機種を足す作業は
# ディレクトリを 1 つ足すことに閉じ、既存ファイルに #ifdef が増えない。
# 手順は arch/README.md。
#   ARCH     = CPU に属するもの   割り込み制御・CPU 停止・記述子表 (arch/x86/)
#   PLATFORM = 機種に属するもの   ポート I/O・I/O ウェイト (platform/pc98/)
ARCH     ?= x86
PLATFORM ?= pc98

# Tools
CC = i386-elf-gcc
AR = i386-elf-ar
AS = nasm
LD = i386-elf-ld
OBJCOPY = i386-elf-objcopy

# === インクルードパス (モジュール別) ===
# 共通: 全カーネルモジュールが参照する基盤ヘッダ
# SDK 公開ヘッダ。-Isdk/include は "os32/xxx.h" 形式、
# -Isdk/include/os32 は既存の "os32_kapi_shared.h" 形式の両方を通すため。
SDK_INC    = -Isdk/include -Isdk/include/os32

# SDK の契約ヘッダ。KernelAPI 構造体の唯一の定義元 (sdk/kapi.json から生成)。
SDK_KAPI_HDR = sdk/include/os32/os32_kapi_shared.h
# -Iarch/$(ARCH) -Iplatform/$(PLATFORM): include/io.h が末尾で引く固定名
# "arch_io.h" / "platform_io.h" をここで解決する。
INC_COMMON = -I. -Iinclude -Iarch/$(ARCH) -Iplatform/$(PLATFORM) $(SDK_INC)

# カーネルコア: 自身 + ドライバ + fs + exec + shell + gfx + lib + kapi
# (kernel.c は全サブシステムの初期化を行うため全モジュールを参照)
INC_KERNEL = $(INC_COMMON) -Ikernel -Idrivers -Inet -Ifs -Iexec -Igfx -Ilib -Ikapi -Ilib/sqlite3

# ドライバ: 共通 + 自身 + gfx (kcg->gfx依存)
INC_DRIVERS = $(INC_COMMON) -Idrivers -Igfx -Ilib

# GFX: 共通 + 自身 + ドライバ (palette依存) + FS (dump時のファイル出力)
INC_GFX = $(INC_COMMON) -Igfx -Idrivers -Ifs -Ilib -Ikernel

# FS: 共通 + 自身 + ドライバ (disk/ide依存)
INC_FS = $(INC_COMMON) -Ifs -Ifs/fatfs -Idrivers -Ikernel -Ilib

# FatFS: fs/fatfs/ ディレクトリ内のソースを独自の string.h スタブで解決
# -Ifs/fatfs を先頭に指定して <string.h> → fs/fatfs/string.h へ誘導
INC_FATFS = -Ifs/fatfs $(INC_COMMON) -Ifs -Idrivers -Ikernel -Ilib

# exec: 共通 + exec + kapi + fs + gfx + ドライバ (kbd依存)
INC_EXEC = $(INC_COMMON) -Iexec -Ikapi -Ifs -Igfx -Idrivers -Ilib -Ikernel

# KAPI: 全モジュール (全APIラッパーのため)
INC_KAPI = $(INC_COMMON) -Ikapi -Ikernel -Idrivers -Inet -Ifs -Iexec -Igfx -Ilib -Ilib/sqlite3

# lib: 共通 + 自身 (汎用ライブラリ: カーネル依存なし)
INC_LIB = $(INC_COMMON) -Ilib

# SQLite: カーネルFS/ドライバ + SQLiteヘッダ (VFS実装用)
INC_SQLITE = $(INC_COMMON) -Ilib/sqlite3 -Ifs -Idrivers -Ilib -Ikernel

# === コンパイルフラグ ===
# -MMD -MP: ヘッダ依存を .d ファイルに出力する (Makefile 末尾で -include する)。
#   これが無いと構造体定義を変えても .o が再ビルドされず、
#   同じ構造体を異なるレイアウトで扱う .o が混在して実行時に壊れる。
DEPFLAGS = -MMD -MP

# カーネル空間とユーザー空間で共通の素性 (フリースタンディング i386 コード)。
# ここには「どちらの空間か」を示すマクロを入れないこと。
#
# -fsigned-char / -fno-short-enums は **ABI の前提を明示に固定する** ためのもの。
# どちらも i386 GCC では既定と同じなので、付けても生成物は 1 バイトも変わらない
# (i386-elf-gcc でカーネル C 92 本 (順序 4-b で lib/kstring_c.c が加わり 93 本)を新旧フラグでコンパイルし、.o の md5 が
#  全本一致することを確認済み)。目的は今の挙動を変えることではなく、
# **暗黙の既定に頼っている箇所を将来 ARM で黙って壊させない** ことにある。
#
#   -fsigned-char    ARM EABI は `char` が **unsigned** が既定。今のコードが
#                    符号に依存している箇所は見つかっていない (utf8.c は
#                    `& 0x80`、iso9660.c は `(unsigned char)` に明示キャスト) が、
#                    `kstrcmp` はまだ lib/kstring_asm.asm の x86 アセンブリで、
#                    これを C 版に起こす (順序 4) と `*a - *b` の符号で
#                    0x80 以上のバイト = 日本語ファイル名の**並び順が変わる**。
#                    移植の最中に既定が裏返らないよう、今のうちに固定しておく。
#   -fno-short-enums ARM EABI は `-fshort-enums` が既定 (enum を収まる最小の
#                    サイズに縮める)。`sdk/include/os32/os32_kapi_shared.h` の
#                    `exec_status_t` のように **カーネルと外部プログラムで
#                    共有する ABI ヘッダ**に enum があるため、enum の幅が
#                    両側で食い違うと構造体のレイアウトごとずれる ([ABI1])。
#                    int 幅に固定する。
CFLAGS_COMMON = -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector \
                -nostdlib -mno-red-zone -fcommon -fsigned-char -fno-short-enums $(DEPFLAGS)

# カーネル空間。__KERNEL_BUILD__ は include/os32_kapi_shared.h が
# memmap.h と KAPI_ADDR を出すかどうかの判定に使う。
KERNEL_CFLAGS = $(CFLAGS_COMMON) -O2 -Wall -D__KERNEL_BUILD__
# 計測やデバッグ用の一時フラグをコマンドラインから足す口。
#   例: make kernel KERNEL_CFLAGS_EXTRA=-DKAPI_PROFILE
KERNEL_CFLAGS += $(KERNEL_CFLAGS_EXTRA)

# LAN (LGY-98)。**既定で有効** (Host Services を製品機能に、ユーザー決裁 2026-09-14)。
#   既定 (stamp 無し) = 有効・FLAGS 0 (driver + リンク層のみ、DIAG/REFLECT/LINKTEST 無し)。
#   カード未装着なら lgy98_init が何もせず起動を続ける (kernel.c、CONFIG_LGY98_BASE 判定)。
#   試験カーネル: make kernel-lgy98 (反射 5, M2/M3/M4) / kernel-lgy98-link (リンク層 L0 試験 9)。
#   無効化: make kernel-nolgy98 (stamp に off = LAN を全く積まない、8MB 機や非対応環境用)。
#   選択は $(LGY98_STAMP) に残るので以後の make kernel / deploy-nhd も同じ設定でビルドする。
#   BASE / IRQ / FLAGS は LGY98_BASE / LGY98_IRQ / LGY98_FLAGS で上書きできる
#   (既定は NP21/W の値。FLAGS = LGY98_FLAG_* の和。DIAG 1 / LOOPBACK 2 / REFLECT 4 / LINKTEST 8)。
#   読むのは drivers/lgy98.c だけで、その .o は毎回コンパイルされる (build/kernel.mk)。
LGY98_STAMP = $(BUILD_OUT)/lgy98.flags
LGY98_BASE  ?= 0x10D0
LGY98_IRQ   ?= 5
ifeq ($(wildcard $(LGY98_STAMP)),)
# stamp 無し = 製品既定: LAN 有効・FLAGS 0
LGY98       ?= 1
LGY98_FLAGS ?= 0
else ifeq ($(strip $(shell cat $(LGY98_STAMP))),off)
# kernel-nolgy98: LAN を積まない (LGY98 未定義のまま)
else
# 試験カーネル: stamp のフラグ値 (reflect 5 / linktest 9)
LGY98       ?= 1
LGY98_FLAGS ?= $(strip $(shell cat $(LGY98_STAMP)))
endif
LGY98_FLAGS ?= 0
ifdef LGY98
KERNEL_CFLAGS += -DCONFIG_LGY98_BASE=$(LGY98_BASE) -DCONFIG_LGY98_IRQ=$(LGY98_IRQ) -DCONFIG_LGY98_FLAGS=$(LGY98_FLAGS)
endif

# ユーザー空間。__KERNEL_BUILD__ を付けないので KAPI テーブルの固定アドレスは
# 見えない。外部プログラムは main() の第3引数で KernelAPI を受け取る。
USER_CFLAGS   = $(CFLAGS_COMMON) -O2 -Wall -D__OS32_USERLAND__

# 旧名。既存の参照が残っている間の互換のために残す。
CFLAGS_BASE = $(KERNEL_CFLAGS)

# SQLite専用フラグ: -Os (サイズ最適化, -O0のスタック肥大化回避) + -Wno-long-long (int64リテラル)
CFLAGS_SQLITE = $(CFLAGS_COMMON) -Os -ffunction-sections -fdata-sections \
                -Wno-long-long -w -DNDEBUG -D__KERNEL_BUILD__
LDFLAGS = -m elf_i386 -T build/os32.ld -Map=$(BUILD_OUT)/kernel.map -nostdlib --nmagic --gc-sections \
	-L$(shell $(CC) -print-libgcc-file-name | xargs dirname)

# === 外部プログラム用フラグ ===
# 外部プログラムの素のインクルードパス。
#   -Iuserland/lib   ランタイム小物を "rt/dbgserial.h" 形式で引く
#   -Iinclude        include/os32_kapi_shared.h ほか
# ライブラリ固有の -I はここには置かない。build/libs.mk の INC_<lib> を
# 必要なターゲットだけに渡すこと (一括で並べると層の逆流が隠れる)。
#
# -Iarch/$(ARCH) -Iplatform/$(PLATFORM) は **足さない**。外部プログラムは
# CPL=3 で走り、in/out も cli/sti も #GP になるので include/io.h を引く
# ソースが 1 本も無い (2026-09-15 に全ツリーを grep して確認。番人は
# tools/check_privileged.py)。もし将来ユーザーランドが io.h を引くように
# なったら、それは特権命令を呼ぼうとしている印なので、-I を足す前に
# 「なぜ CPL=3 のコードが原始命令を要るのか」を先に疑うこと。
PROGRAM_FLAGS = $(USER_CFLAGS) -I. -Iinclude $(SDK_INC) -Iuserland/lib -I$(CROSS_DIR)/i386-elf/include
PROGRAM_LDFLAGS = -m elf_i386 -T sdk/link/app.ld -nostdlib --nmagic --gc-sections \
	-L$(LIBDIR) -L$(CROSS_DIR)/i386-elf/lib -L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0

# ライブラリアーカイブは --start-group で囲む。アーカイブは左から順に
# 一度しか走査されないため、並び順が依存順と食い違うと未解決シンボルになる。
# グループで囲めば解決しなくなるまで反復するので、並び順を気にしなくてよい。
LGRP_BEG = --start-group
LGRP_END = --end-group

# === CRT0 / デバッグオブジェクト ===
CRT0_OBJ = sdk/crt/crt0.o sdk/crt/crt0_c.o sdk/crt/syscalls.o sdk/crt/help.o
DBG_OBJ  = userland/lib/rt/dbgserial.o

# === デプロイ先 ===
HOSTDRV_DIR ?= /mnt/c/os32
NHD_DEPLOY = env NP21W_DIR='$(NP21W_DIR)' python3 tools/nhd_deploy.py
HOSTDRV_DEPLOY = env HOSTDRV_DIR='$(HOSTDRV_DIR)' python3 tools/hostdrv_deploy.py
