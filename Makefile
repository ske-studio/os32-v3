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
BUILD_ID_SRC = $(BUILD_OUT)/build_id.c
include build/build-id.mk
ABI_CONTRACT_ORDER := abi-contract-probe
endif
# 製品の規則。TARGET_MK は試験が写しを差し替える時だけ与える。
TARGET_MK ?= build/targets/$(ARCH)-$(PLATFORM).mk
include $(TARGET_MK)
include build/abi_contract.mk

# The bootstrap remains independent; always forward its own cross directory.
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
.PHONY: ppc-boot
ppc-boot:
	$(MAKE) --no-print-directory -j1 -f build/ppc.mk CROSS_DIR=$(PPC_CROSS_DIR) all
# ドキュメントを Windows 側 (C:\WATCOM\docs\os32) へミラー / 逆方向は tools/sync_hwdocs.sh
.PHONY: docs-gen docs-win

docs-gen:
	python3 tools/gen_memmap.py --write

docs-win:
	sh tools/sync_docs_to_win.sh

# D35: retain the checked link inputs and final ELF for packaging/manifest.
.PRECIOUS: %.elf
