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
#  docs/08_build.md・票 docs/archive/tools/TASK_CHECK_MUT_PARALLEL.md §5)。
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
ifeq ($(ARCH),x86)
ABI_CONTRACT_ORDER := abi-contract-probe
endif
include build/targets/$(ARCH)-$(PLATFORM).mk
include build/abi_contract.mk

# The bootstrap remains independent; always forward its own cross directory.
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
.PHONY: ppc-boot ppc-boot-check
ppc-boot:
	$(MAKE) --no-print-directory -j1 -f build/ppc.mk CROSS_DIR=$(PPC_CROSS_DIR) all
ppc-boot-check:
	$(MAKE) --no-print-directory -j1 -f build/ppc.mk CROSS_DIR=$(PPC_CROSS_DIR) check

# ドキュメントを Windows 側 (C:\WATCOM\docs\os32) へミラー / 逆方向は tools/sync_hwdocs.sh
.PHONY: docs-gen tests-inventory docs-win
tests-inventory:
	python3 tools/gen_tests_inventory.py --write

docs-gen: tests-inventory
	python3 tools/gen_memmap.py --write

docs-win:
	sh tools/sync_docs_to_win.sh

# D35: retain the checked link inputs and final ELF for packaging/manifest.
.PRECIOUS: %.elf
