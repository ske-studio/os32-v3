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
include build/kdebug.mk
include build/hwdev.mk
include build/sqlite-proto.mk
include build/fep-module.mk
include build/cd-module.mk
include build/kcg-module.mk
include build/fm-module.mk
include build/pcm-module.mk
include build/v86-module.mk
include build/lan-module.mk
include build/cirrus-module.mk
include build/geforce-module.mk
include build/libs.mk
include build/programs.mk
include build/deploy.mk
include build/image.mk
include build/sdk.mk
include build/assets.mk

# First-build ordering, before compiler-generated .d files exist.
MODULE_PROVIDER_OBJS := $(SQLITE_PROTO_OUT)/sqlite_provider_types.o \
    $(FEP_OUT)/fep_provider_types.o $(CD_OUT)/cd_provider_types.o \
    $(KCG_OUT)/kcg_provider_types.o $(FM_OUT)/fm_provider_types.o \
    $(PCM_OUT)/pcm_provider_types.o $(V86_OUT)/v86_provider_types.o \
    $(LAN_OUT)/lan_provider_types.o $(CIRRUS_OUT)/cirrus_provider_types.o $(GEFORCE_OUT)/geforce_provider_types.o
$(C_KERNEL_OBJ) $(ASM_KERNEL_OBJ) $(BUILD_ID_OBJ) $(R1_OBJECTS) \
    $(KDEBUG_OBJS) $(SQLITE_PROTO_OBJS) $(FEP_OBJS) $(CD_OBJS) $(KCG_OBJS) \
    $(FM_OBJS) $(PCM_OBJS) $(V86_OBJS) $(LAN_OBJS) $(CIRRUS_OBJS) $(GEFORCE_OBJS) \
    $(ALL_LIB_OBJ) $(CRT0_USER_OBJ) $(CRT0_RESIDENT_OBJ) $(DBG_OBJ) \
    $(SHELL_OBJ) $(SH_OBJ) $(PCI_DECODE_USER_OBJ) $(PC98PT_USER_OBJ) \
    $(SH_PCI_DECODE_OBJ) $(SH_PC98PT_OBJ) $(INST_OBJ) $(BENCH_OBJ) \
    $(BENCH_S2X_OBJ) $(FAULTPROBE_OBJ) $(SQLITE_SA_OBJ) \
    $(patsubst %.c,%.o,$(wildcard userland/cmds/*.c userland/tests/*.c userland/system/*.c)) \
    lib/utf8_prog.o lib/lz4_prog.o lib/microtar/microtar_prog.o userland/lib/rt/pkg.o \
    userland/lib/gfx/ui.o $(MODULE_PROVIDER_OBJS) \
    userland/tests/kstr_asm_a.o userland/tests/kstr_c_c.o \
    $(RUST_LZ4_LIB) $(RUST_KAPI_RS) $(SDK_KAPI_HDR) sdk: | $(KAPI_GENERATED)

$(GSHELL_LIB) sdk: | $(RUST_KAPI_RS)

# module_image.h is also included transitively by core and component units.
$(C_KERNEL_OBJ) $(R1_OBJECTS) $(KDEBUG_OBJS) $(FEP_OBJS) $(CD_OBJS) \
    $(KCG_OBJS) $(FM_OBJS) $(PCM_OBJS) $(V86_OBJS) $(LAN_OBJS) $(CIRRUS_OBJS) $(GEFORCE_OBJS) \
    $(MODULE_PROVIDER_OBJS): | $(MODULE_SCHEMA)

# === ヘッダ依存の取り込み ===
# 各 .c のコンパイル時に -MMD -MP (build/config.mk の DEPFLAGS) が生成する .d を読み込む。
# これにより構造体定義などヘッダを変更したとき、依存する .o が必ず再ビルドされる。
# Rust の target/ 以下にも .d があるため除外する。
# Ignore legacy core .d files beside sources, including lib/: those describe
# the old objects. User-side lib/*_prog.d stays in scope until the 6b task.
DEPFILES := $(C_KERNEL_OBJ:.o=.d) $(BUILD_ID_OBJ:.o=.d) \
            $(shell find $(BUILD_OUT) boot programs userland sdk \
              -name '*.d' -not -path '*/target/*' 2>/dev/null) \
            $(shell find lib -name '*_prog.d' -not -path '*/target/*' 2>/dev/null)
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
all: boot $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/vmkernel.lz4 $(KERNEL_PAIRED_MODULES) \
     images/os32_boot.d88 images/os32_boot144.img programs sdk assets-deployed iso
	# 全成果物が揃った世代表に配置表を合わせる (kernel 単独の表より広い集合)。
	@python3 tools/gen_generation_manifest.py
	@python3 tools/gen_memmap.py --manifest --write
	@python3 tools/gen_deploy_set.py

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
	python3 -c "import shutil; shutil.rmtree('build/out/host32-controls', ignore_errors=True)"

# 依存ファイル (.d) の一括削除 — Rust の target/ 以下は対象外
clean-deps:
	@find $(BUILD_OUT) boot kernel drivers platform net gfx fs exec kapi lib programs userland sdk \
	  -name '*.d' -not -path '*/target/*' -delete 2>/dev/null || true

.PHONY: all clean clean-deps

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
