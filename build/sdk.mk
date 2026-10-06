# ============================================================================
#  sdk.mk — OS32 SDK の staging
#
#  外部プログラムが OS32 向けにビルドするために必要なものだけを
#  build/sdk/ に固める。ここに入っていないものに依存するプログラムは、
#  リポジトリを分割したときにビルドできなくなる。
# ============================================================================

SDK_OUT = build/sdk

# 公開ヘッダを SDK に載せるプラットフォームライブラリ。
# rt/ は "rt/dbgserial.h" 形式で引くので別扱い (下のルール参照)。
SDK_LIB_HEADER_DIRS = math gfx db ui input asset snd tilemap md filer mgx save ecs cfg

# kapi.json の version が KAPI バージョンの唯一の情報源。
# ヘッダ・Rust バインディング・ドキュメントはすべてここから導出する。
KAPI_VERSION := $(shell python3 -c "import json;print(json.load(open('sdk/kapi.json'))['version'])")

sdk: $(ALL_LIB_ARCHIVES) $(CRT0_OBJ) $(DBG_OBJ) $(SDK_KAPI_HDR)
	@rm -rf $(SDK_OUT)
	@mkdir -p $(SDK_OUT)/include/os32 $(SDK_OUT)/lib $(SDK_OUT)/crt \
	          $(SDK_OUT)/link $(SDK_OUT)/bin $(SDK_OUT)/rust
	cp sdk/include/os32/*.h              $(SDK_OUT)/include/os32/
	@# プラットフォームライブラリの公開ヘッダ。アーカイブだけ配っても
	@# ヘッダが無ければ使えない。internal と付くものは実装内部なので除く。
	@for d in $(SDK_LIB_HEADER_DIRS); do \
	    for h in userland/lib/$$d/*.h; do \
	        case "$$h" in *_internal.h) continue;; esac; \
	        cp "$$h" $(SDK_OUT)/include/ || exit 1; \
	    done; \
	done
	@mkdir -p $(SDK_OUT)/include/rt
	cp userland/lib/rt/*.h                $(SDK_OUT)/include/rt/
	@# 共有 C の公開ヘッダ。実装は libos32gfx.a に入っている (utf8_prog.o)。
	cp lib/utf8.h                         $(SDK_OUT)/include/
	cp $(LIBDIR)/*.a                     $(SDK_OUT)/lib/
	cp $(CRT0_OBJ) $(DBG_OBJ)            $(SDK_OUT)/crt/
	cp sdk/crt/generations.inc            $(SDK_OUT)/crt/
	cp sdk/link/*.ld                     $(SDK_OUT)/link/
	cp sdk/mkos32x.py                    $(SDK_OUT)/bin/
	@# mkos32x.py が import するヘッダ v3 の共通モジュール (票 TASK_KAPI_DATA_FIELDS)
	cp sdk/link_guard.py sdk/rustc_stamp.py $(SDK_OUT)/bin/
	cp sdk/os32_generations.py          $(SDK_OUT)/bin/
	cp sdk/os32x_hdr.py                  $(SDK_OUT)/bin/
	cp sdk/rust/i686-os32-none.json      $(SDK_OUT)/rust/
	cp -r sdk/rust/os32api               $(SDK_OUT)/rust/
	@rm -rf $(SDK_OUT)/rust/os32api/target
	@# 動くサンプル。SDK だけでビルドできることの実証も兼ねる。
	@mkdir -p $(SDK_OUT)/example
	cp -r sdk/example/hello              $(SDK_OUT)/example/
	@rm -f $(SDK_OUT)/example/hello/*.o $(SDK_OUT)/example/hello/*.elf \
	       $(SDK_OUT)/example/hello/*.raw $(SDK_OUT)/example/hello/*.bin
	cp sdk/README.md                     $(SDK_OUT)/
	@echo $(KAPI_VERSION) > $(SDK_OUT)/KAPI_VERSION
	@echo "=== OS32 SDK $(SDK_OUT) (KAPI v$(KAPI_VERSION)) ==="
	@echo "  include       $$(ls $(SDK_OUT)/include/*.h | wc -l) lib headers"
	@echo "  include/os32  $$(ls $(SDK_OUT)/include/os32 | wc -l) contract headers"
	@echo "  include/rt    $$(ls $(SDK_OUT)/include/rt | wc -l) runtime headers"
	@echo "  lib           $$(ls $(SDK_OUT)/lib | wc -l) archives"
	@echo "  crt           $$(ls $(SDK_OUT)/crt | wc -l) objects"
	@echo "  link          $$(ls $(SDK_OUT)/link | wc -l) linker scripts"
	@echo "  example       hello (SDK のみでビルドできる最小例)"

# ----------------------------------------------------------------------------
#  配布用 tarball
#
#  サードパーティが OS のソースを持たずにアプリを作れるようにするための
#  成果物。KAPI バージョンをファイル名に入れてあるので、どの OS 世代向けに
#  ビルドされたアプリかが一目で分かる。
#
#  展開して OS32_SDK を向けるだけで使える:
#    tar xzf os32-sdk-39.tar.gz
#    make -C myapp OS32_SDK=$(pwd)/os32-sdk-39
# ----------------------------------------------------------------------------
SDK_DIST_NAME = os32-sdk-$(KAPI_VERSION)
SDK_DIST_DIR  = build/dist

sdk-dist: sdk
	@rm -rf $(SDK_DIST_DIR)/$(SDK_DIST_NAME)
	@mkdir -p $(SDK_DIST_DIR)
	cp -r $(SDK_OUT) $(SDK_DIST_DIR)/$(SDK_DIST_NAME)
	@python3 tools/gen_sdk_manifest.py $(SDK_DIST_DIR)/$(SDK_DIST_NAME)
	tar czf $(SDK_DIST_DIR)/$(SDK_DIST_NAME).tar.gz \
	        -C $(SDK_DIST_DIR) $(SDK_DIST_NAME)
	@rm -rf $(SDK_DIST_DIR)/$(SDK_DIST_NAME)
	@echo "=== $(SDK_DIST_DIR)/$(SDK_DIST_NAME).tar.gz "\
	      "($$(stat -c%s $(SDK_DIST_DIR)/$(SDK_DIST_NAME).tar.gz) bytes) ==="

# check は 1 段 (2026-09-26 から)。変異試験は**一時ディレクトリの写しの木**に
# 変異を当てる (tools/tests/mutpar.py の overlay / mutant_tree、票
# docs/archive/tools/TASK_CHECK_MUT_PARALLEL.md §5) ので、全部を並列に回せる。
# 以前は実物のソースを書き換えて戻す試験が 14 本あり、2 段目 check-mut を
# 逐次 (-j1) で回していた (docs/POLICY_DEBUG.md §4-40・§4-41)。
#
# 段の前後で tools/check_tree_unchanged.py が「試験がソースを書き換えたまま
# 戻していないか」を見る (番人)。引っかかったら、その試験を写しの作りに直す
# — **実物を書き換える試験を列に足さない**。
#
# 検査は 3 通りの回し方がある (2026-09-26、docs/08_build.md §8-4)。列と recipe は
# 共通で、違うのは**変異 (否定側) を回すかどうか**だけ:
#
#   make check-fast     全部の検査を変異なしで並列に。作業中に何度でも
#   make check-changed  変更したファイルに関係する検査だけ変異込み、残りは
#                       変異なし (BASE=<ref>、既定は feat/gui との merge-base)。
#                       対応表は tools/check_map.d/、選び方は tools/check_select.py
#   make check          全部を変異込み。取り込みの前に 1 回
#
# 変異の有無は MUTATE で切り替える。recipe は --mutate / --mutants の代わりに
# $(MUT) / $(MUTS) と書く。MUTATE=1 で全部、MUTATE=0 で無し、MUTATE=sel なら
# MUTATE_TARGETS に名前のある検査だけ ($@ で引く)。
MUTATE ?= 1
MUTATE_TARGETS ?=
mut_on = $(or $(filter 1,$(MUTATE)),$(filter $@,$(MUTATE_TARGETS)))
MUT = $(if $(mut_on),--mutate)
MUTS = $(if $(mut_on),--mutants)

check:
	@python3 tools/check_tree_unchanged.py --save par
	@$(MAKE) check-par MUTATE=1
	@python3 tools/check_tree_unchanged.py --verify par

check-fast:
	@python3 tools/check_tree_unchanged.py --save fast
	@$(MAKE) $(CHECK_PAR_TARGETS) MUTATE=0
	@python3 tools/check_tree_unchanged.py --verify fast

# 並列 1 段 (選ばれた検査だけ変異込み、残りは変異なし)。
# FILES="<パス>..." を渡すと git を見ずにその一覧を変更とみなす (選び方の試し。
# `FILES=` と空で渡せば「変更なし」)。
BASE ?=
check-changed:
	@mkdir -p $(BUILD_OUT)
	@python3 tools/check_select.py --select --base "$(BASE)" \
	  $(if $(filter command line,$(origin FILES)),--files $(FILES)) > $(BUILD_OUT)/check_changed.sh
	@. $(BUILD_OUT)/check_changed.sh; \
	  python3 tools/check_tree_unchanged.py --save chg1 && \
	  { [ -z "$$CC_STAGE1" ] || $(MAKE) $$CC_STAGE1 MUTATE=sel MUTATE_TARGETS="$$CC_MUT1"; } && \
	  python3 tools/check_tree_unchanged.py --verify chg1 && \
	  echo "*** $$CC_SUMMARY ***"

clean-sdk:
	rm -rf $(SDK_OUT) $(SDK_DIST_DIR)

.PHONY: check-fast check-changed check-par sdk sdk-dist clean-sdk check

# Every check owns its recipe and registration. Numeric keys preserve historical order.
HOST32_RUNNERS ?= native qemu
export HOST32_RUNNERS
ifndef OS32_CONTROL_SESSION
export OS32_CONTROL_SESSION := $(shell python3 -c "import uuid; print(uuid.uuid4().hex)")
endif

define host32_check
@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/$(1) --runner $$runner; done
$(if $(MUT),python3 -B tools/tests/$(1) --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)
endef
CHECK_PAR_ORDER :=
include $(sort $(wildcard build/checks.d/*.mk))
CHECK_PAR_TARGETS = $(foreach entry,$(sort $(CHECK_PAR_ORDER)),$(word 2,$(subst :, ,$(entry))))

check-par: $(CHECK_PAR_TARGETS)
