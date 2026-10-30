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

sdk: $(ALL_LIB_ARCHIVES) $(CRT0_USER_OBJ) $(DBG_OBJ) $(SDK_KAPI_HDR)
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
#   make check-changed  段と当たりで列を選び、当たりだけ変異込み
#                        (BASE=<ref>、既定は main 等との merge-base)。
#                       CHECK 宣言と tools/checks/run.py で段と列を選ぶ
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

# 親で前提を一度準備してから、ソース保存・計時付き並列実行・照合の順。
check:
	+@$(MAKE) check-prepare
	@python3 tools/check_tree_unchanged.py --save par
	+@$(MAKE) check-par CHECK_NEEDS= MUTATE=1
	@python3 tools/check_tree_unchanged.py --verify par

check-fast:
	+@$(MAKE) check-prepare
	@python3 tools/check_tree_unchanged.py --save fast
	+@$(MAKE) check-par CHECK_NEEDS= MUTATE=0
	@python3 tools/check_tree_unchanged.py --verify fast

# FILES= は明示的な変更なし。取り込みのコード変更は段2以上。
BASE ?=
check-changed: check-times-init
	@mkdir -p $(BUILD_OUT)
	@python3 -B tools/checks/run.py --select --tier 2 \
	  $(if $(filter command line,$(origin FILES)),--files $(FILES),--base "$(BASE)") > $(BUILD_OUT)/check_changed.sh
	+@. $(BUILD_OUT)/check_changed.sh; \
	  export CC_FILES_JSON CC_TIER; \
	  if [ "$(MUTATE)" = 0 ]; then CC_MUT1=; fi; \
	  if [ -n "$$CC_STAGE1" ]; then \
	    $(MAKE) check-prepare CHECK_NEEDS="$$CC_NEEDS" && \
	    python3 tools/check_tree_unchanged.py --save chg1 && \
	    $(MAKE) check-par CHECK_PAR_TARGETS="$$CC_STAGE1" CHECK_NEEDS= MUTATE=sel MUTATE_TARGETS="$$CC_MUT1" && \
	    python3 tools/check_tree_unchanged.py --verify chg1 || exit $$?; \
	  fi; \
	  echo "*** $$CC_SUMMARY ***"

clean-sdk:
	rm -rf $(SDK_OUT) $(SDK_DIST_DIR)

.PHONY: check-fast check-changed check-par sdk sdk-dist clean-sdk check

# CHECK が唯一の登録元。列挙失敗を空列として扱わない。
HOST32_RUNNERS ?= native qemu
export HOST32_RUNNERS
export MUTATE MUTATE_TARGETS BUILD_OUT SQLITE_PROTO_OUT CROSS_DIR KERNEL_CFLAGS INC_KERNEL C_STD
ifndef OS32_CONTROL_SESSION
export OS32_CONTROL_SESSION := $(shell python3 -c "import uuid; print(uuid.uuid4().hex)")
endif
# 計時子は親が確定した本体だけを実行する。登録の再列挙・準備は不要。
ifeq ($(CHECK_TIME_CHILD),1)
CHECK_NAMES := $(filter check-%,$(MAKECMDGOALS))
CHECK_ALL_NEEDS :=
else
CHECK_NAMES := $(shell python3 -B tools/checks/run.py --list)
ifneq ($(.SHELLSTATUS),0)
$(error tools/checks/run.py --list failed)
endif
CHECK_ALL_NEEDS := $(shell python3 -B -c 'import json, subprocess; rows = json.loads(subprocess.check_output(["python3", "-B", "tools/checks/run.py", "--list", "--json"])); print(" ".join(sorted({n[5:] for c in rows for n in c["needs"] if n.startswith("make:")})))')
ifneq ($(.SHELLSTATUS),0)
$(error tools/checks/run.py --list --json failed)
endif
endif
CHECK_PAR_TARGETS ?= $(CHECK_NAMES)
CHECK_NEEDS ?= $(CHECK_ALL_NEEDS)

.PHONY: $(CHECK_NAMES) check-prepare check-par-run
$(CHECK_NAMES): check-%:
	+@$(if $(findstring n,$(filter-out --%,$(firstword $(MAKEFLAGS)))),echo ,)python3 -B tools/checks/run.py --run $@

check-prepare:
	+@$(if $(strip $(CHECK_NEEDS)),$(MAKE) $(CHECK_NEEDS),:)

CHECK_TIMES ?= build/out/check_times.tsv
.PHONY: check-times-init $(addprefix check-time-,$(CHECK_NAMES))
check-times-init:
	@python3 tools/check_time.py --reset "$(CHECK_TIMES)"

# 準備に失敗したら計時子を起動しない。子は指定した本体だけを実行する。
check-par: check-times-init check-prepare
	+@$(MAKE) check-par-run
check-par-run: $(addprefix check-time-,$(CHECK_PAR_TARGETS))
$(addprefix check-time-,$(CHECK_NAMES)): check-time-%:
	+@python3 tools/check_time.py "$(CHECK_TIMES)" "$*" $(MAKE) --no-print-directory CHECK_TIME_CHILD=1 $*

# 列外の計測器・ローカル検査と既存の別名は手書きで残す。
check-e10c-lifecycle: check-memmap-audit-host
.PHONY: check-e10c-lifecycle

# ARM コンパイル計測 (移植性準備の順序 1)。カーネル側の C ソースを 1 本ずつ
# arm-none-eabi-gcc に通し、通った本数と失敗の分類を出す。
#
# **`check` の列にはわざと入れていない。** これは合否の門ではなく計測器で、
# 今 ARM で通らないのは当たり前 (x86 前提でよい、と決めて書いてある)。
# 門にすると「直さないと緑にならない」圧力がかかり、まだ設計の決まっていない
# arch/ の分離を急がせてしまう。io.h 経由への統一 (順序 2) や arch/ 導入
# (順序 3) の効果を同じ物差しで見るために、独立したターゲットとして呼ぶ。
#
# arm-none-eabi-gcc が無い環境では SKIP して終了コード 0。
check-arm-compile:
	@python3 tools/check_arm_compile.py

.PHONY: check-arm-compile

# PM-local only (not in CHECK_PAR_ORDER, so CI never runs it): the nvbios transcript
# (~/os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt, override with GFINIT_SOURCE) is VBIOS-derived
# and stays out of the repo. No ROM, hardware, decoder or GPU execution. Ledger: GF4A-S1-FIXTURE.
check-gfinit-fixture-host:
	python3 -B tools/tests/test_gfinit_fixture.py $(MUT)

.PHONY: check-gfinit-fixture-host

# Local only: maintenance suspended by user decision 2026-10-24 (hardware unavailable).
# tools/host_agent.py v2 (ワイヤ v2 の Agent 側、票 N1 段 1)。贋 OS32 が
# フレームを直接組んで rid 台帳 / 3 way HELLO / 墓標 / 枯渇停止を踏む。
check-host-agent:
	python3 -B tools/tests/test_host_agent.py

.PHONY: check-host-agent

# Local only: maintenance suspended by user decision 2026-10-24 (hardware unavailable).
# libos32host + wget/lpr/hclip/hdate のホスト TDD (票 N3)。実物のソースを #include し、
# KAPI / libos32host の関数だけを贋物に。記録は tools/tests/n3_tdd.md。
check-host-lib-host:
	python3 -B tools/tests/test_host_lib.py --target

.PHONY: check-host-lib-host

# Local only: maintenance suspended by user decision 2026-10-24 (hardware unavailable).
# tools/lan_bridge.py (実機の NIC ↔ host_agent.py の橋、票 TASK_LAN_82557 L-D)。
# root も実 NIC も要らない — 橋の `--fake-nic` (NIC の差し替え口) に贋 OS32 を
# UNIX ソケットで繋ぎ、反対側には **実物の host_agent.py** を `--unix` で子プロセス
# 起動して、L0 (3 way HELLO) + PING が**両方向**通ることを見る。
# 見るのは 4 つ: FrameStream の枠付けが host_agent.py とバイト単位で同じか、
# EtherType 0x88B5 以外を Agent へ流さないか、逆向き (agent -> NIC) が生きているか、
# 実 NIC を開けないときに CAP_NET_RAW を名指して止まるか。
# **AF_PACKET の経路そのものはここでは踏めない** — 実機 + Ubuntu ノートで PM が見る。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/lan_bridge_tdd.md。
check-lan-bridge-host:
	python3 -B tools/tests/test_lan_bridge.py $(MUT)

.PHONY: check-lan-bridge-host

# Local only: maintenance suspended by user decision 2026-10-24 (hardware unavailable).
# net/link.c (ワイヤ v2) + kapi/kapi_host.c (KAPI v51) のホスト TDD (票 N1 段 4)。
# 実物のソースを #include し、NIC / cli-sti / 100Hz タイマ / ディスパッチャだけを
# 贋物にする。対向は **実 Agent** (host_agent.py を UNIX ソケットで子プロセス起動)
# か台本。記録は tools/tests/n1_tdd.md。
check-net-link-host:
	python3 -B tools/tests/test_net_link.py --target

.PHONY: check-net-link-host

check-guest:
	python3 tools/guest_tests.py

.PHONY: check-guest
