# ============================================================================
#  PC-9801 OS32 Makefile for GCC/NASM
#
#  リファクタリング版: build/*.mk による分割構成
#
#  主要ターゲット:
#    make all          全ビルド (カーネル + プログラム + イメージ)
#    make kernel       カーネル + SQLite のみ
#    make libs         全ユーザー空間ライブラリのみ
#    make programs     全外部プログラム
#    make boot         ブートローダーのみ
#    make deploy       HostDrvデプロイ
#    make deploy-kernel NHDブート領域書き込み
#    make clean        全クリーン
#
#  個別ターゲット:
#    make lib-<name>   個別ライブラリ (例: make lib-libos32math)
#    make <test_name>  個別テスト (例: make econ_test)
# ============================================================================

# ----------------------------------------------------------------------------
#  並列ビルド (2026-09-17)
#
#  既定で CPU の数だけ並べる。実測 (16 コア): kernel 23.7 秒 → 11.8 秒、
#  all 70.1 秒 → 31.0 秒。`make -j1` と書けば従来どおり逐次になる。
#
#  `make check` も並列で回る。変異試験は一時ディレクトリの写しの木に変異を
#  当てる作り (tools/tests/mutpar.py) で、実物のソースは読むだけ。以前は
#  実物を書き換えて戻す試験を 2 段目 (-j1) に分けていた (2026-09-26 に解消、
#  docs/POLICY_DEBUG.md §4-40・票 docs/archive/tools/TASK_CHECK_MUT_PARALLEL.md §5)。
# ----------------------------------------------------------------------------
OS32_NPROC := $(shell nproc 2>/dev/null || echo 1)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
  MAKEFLAGS += -j$(OS32_NPROC)
endif

.DEFAULT_GOAL := all

# 環境変数を .env ファイルから読み込み (存在する場合)
-include .env

# === サブ Makefile のインクルード ===
include build/config.mk
include build/boot.mk
include build/kernel.mk
include build/libs.mk
include build/programs.mk
include build/deploy.mk
include build/image.mk
include build/sdk.mk
include build/assets.mk

# === ヘッダ依存の取り込み ===
# 各 .c のコンパイル時に -MMD -MP (build/config.mk の DEPFLAGS) が生成する .d を読み込む。
# これにより構造体定義などヘッダを変更したとき、依存する .o が必ず再ビルドされる。
# Rust の target/ 以下にも .d があるため除外する。
DEPFILES := $(shell find boot kernel drivers gfx fs exec kapi lib programs sdk \
              -name '*.d' -not -path '*/target/*' 2>/dev/null)
-include $(DEPFILES)

# === 汎用パターンルール (ASM) ===
%.bin: %.asm
	$(AS) -f bin $< -o $@

%.o: %.asm
	$(AS) -p sdk/crt/generations.inc -f elf32 $< -o $@

# === 主要ターゲット ===
# ゲームは SDK 経由でビルドするので、programs (= SDK のもとになる
# ライブラリ群) と sdk のあとに置く。iso はパッケージ生成を含むため最後。
# 起動 FD は 2 種とも作る — 中身は CD の MINIMAL と同じ集合なので、MINIMAL に
# 足した物が FD の容量に収まらなければここで落ちる (build/image.mk)。
all: boot $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/sqlite.bin $(BUILD_OUT)/vmkernel.lz4 \
     images/os32_boot.d88 images/os32_boot144.img programs sdk assets-deployed iso
	@python3 tools/gen_generation_manifest.py

# === 外部リポジトリ (git submodule) ===
# apps/ = ske-studio/os32-apps、game/ = ske-studio/os32-game。どちらも SDK だけで
# ビルドする独立リポジトリで、この Makefile は SDK と CROSS_DIR を渡して呼ぶだけ。
# 検証した組み合わせは submodule のポインタとして os32 のコミットに残る。
# 空なら `git submodule update --init` を促す。
EXT_MAKEFLAGS = OS32_SDK=$(abspath build/sdk) CROSS_DIR=$(CROSS_DIR)
define ext_check
	@[ -f $(1)/Makefile ] || { echo "ERROR: $(1)/ is empty — run: git submodule update --init $(1)"; exit 1; }
endef
apps: sdk
	$(call ext_check,apps)
	$(MAKE) -C apps $(EXT_MAKEFLAGS) all
game: sdk
	$(call ext_check,game)
	$(MAKE) -C game $(EXT_MAKEFLAGS) all
external: apps game
clean-external:
	-[ -f apps/Makefile ] && $(MAKE) -C apps $(EXT_MAKEFLAGS) clean
	-[ -f game/Makefile ] && $(MAKE) -C game $(EXT_MAKEFLAGS) clean
.PHONY: apps game external clean-external

# === クリーン (全サブモジュール) ===
clean: clean-kernel clean-programs clean-libs clean-images clean-sdk clean-assets clean-deps
	rm -f os.img os.d88 os_install.img os_install.d88 os_raw.img

# 依存ファイル (.d) の一括削除 — Rust の target/ 以下は対象外
clean-deps:
	@find boot kernel drivers gfx fs exec kapi lib programs sdk \
	  -name '*.d' -not -path '*/target/*' -delete 2>/dev/null || true

.PHONY: all clean clean-deps

# ドキュメントを Windows 側 (C:\WATCOM\docs\os32) へミラー / 逆方向は tools/sync_hwdocs.sh
.PHONY: docs-win
docs-win:
	sh tools/sync_docs_to_win.sh

# D35: retain the checked link inputs and final ELF for packaging/manifest.
.PRECIOUS: %.elf
