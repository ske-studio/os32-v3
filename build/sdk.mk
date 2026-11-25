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
SDK_LIB_HEADER_DIRS = math gfx db ui input asset tilemap md filer mgx save ecs cfg mmtime

# kapi.json の version が KAPI バージョンの唯一の情報源。
# ヘッダ・Rust バインディング・ドキュメントはすべてここから導出する。
KAPI_VERSION := $(shell python3 -c "import json;print(json.load(open('sdk/kapi.json'))['version'])")

sdk: $(ALL_LIB_ARCHIVES) $(NANO_LIB) $(CRT0_USER_OBJ) $(DBG_OBJ) $(SDK_KAPI_HDR)
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
	cp $(ALL_LIB_ARCHIVES) $(NANO_LIB) $(SDK_OUT)/lib/
	cp $(filter %.o,$(CRT0_USER_OBJ)) $(DBG_OBJ)            $(SDK_OUT)/crt/
	cp sdk/crt/generations.inc            $(SDK_OUT)/crt/
	cp sdk/link/*.ld                     $(SDK_OUT)/link/
	cp sdk/mkos32x.py                    $(SDK_OUT)/bin/
	@# mkos32x.py が import するヘッダ v3 の共通モジュール (票 TASK_KAPI_DATA_FIELDS)
	cp sdk/link_guard.py sdk/rustc_stamp.py $(SDK_OUT)/bin/
	cp sdk/allocator/check_link.py $(SDK_OUT)/bin/nano_check_link.py
	cp $(LIBDIR)/libos32nano.json $(SDK_OUT)/lib/
	cp sdk/allocator/nano.LICENSE $(SDK_OUT)/lib/
	cp sdk/os32_generations.py          $(SDK_OUT)/bin/
	cp sdk/target_profile.py             $(SDK_OUT)/bin/
	@# example の Makefile と外部のビルドが target の値を引く口 (第 5 段 S2)
	cp sdk/target_profile_cli.py         $(SDK_OUT)/bin/
	cp sdk/os32x_hdr.py                  $(SDK_OUT)/bin/
	cp $(SDK_TARGET_RUST)      $(SDK_OUT)/rust/
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

clean-sdk:
	rm -rf $(SDK_OUT) $(SDK_DIST_DIR)

.PHONY: sdk sdk-dist clean-sdk

ifneq ($(filter check check-%,$(MAKECMDGOALS)),)
include build/check.mk
endif
