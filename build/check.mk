# check は 1 段 (2026-09-26 から)。変異試験は**一時ディレクトリの写しの木**に
# 変異を当てる (tools/tests/mutpar.py の overlay / mutant_tree、票
# docs/archive/tools/TASK_CHECK_MUT_PARALLEL.md §5) ので、全部を並列に回せる。
# 以前は実物のソースを書き換えて戻す試験が 14 本あり、2 段目 check-mut を
# 逐次 (-j1) で回していた (docs/08_build.md §8-4)。
#
# 段の前後で tools/check_tree_unchanged.py が「試験がソースを書き換えたまま
# 戻していないか」を見る (番人)。引っかかったら、その試験を写しの作りに直す
# — **実物を書き換える試験を列に足さない**。
#
# 検査は 3 通りの回し方がある (2026-09-26、docs/08_build.md §8-4)。列と recipe は
# 共通で、変異 (否定側) の有無と full_only の配置を切り替える:
#
#   make check-fast     full_only 以外を変異なしで並列に。
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
CHECK_EXCLUDE_FLAGS = $(foreach e,$(CHECK_EXCLUDE_ENV),--exclude-env $(e))
check:
ifneq ($(strip $(CHECK_EXCLUDE_ENV)),)
	@python3 -B tools/checks/run.py --excluded $(CHECK_EXCLUDE_FLAGS) $(if $(CHECK_SHARD),--shard $(CHECK_SHARD))
endif
	+@$(MAKE) check-prepare
	@python3 tools/check_tree_unchanged.py --save par
	+@$(MAKE) check-par CHECK_NEEDS= MUTATE=1
	@python3 tools/check_tree_unchanged.py --verify par

check-fast:
	+@$(MAKE) check-prepare CHECK_ROUTINE=1
	@python3 tools/check_tree_unchanged.py --save fast
	+@$(MAKE) check-par CHECK_NEEDS= MUTATE=0 CHECK_ROUTINE=1
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

.PHONY: check-fast check-changed check-par check

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
CHECK_SHARD ?=
CHECK_REGISTRATION := $(shell CHECK_ROUTINE=$(CHECK_ROUTINE) python3 -B tools/checks/run.py --make $(CHECK_EXCLUDE_FLAGS) $(if $(CHECK_SHARD),--shard $(CHECK_SHARD)))
ifneq ($(.SHELLSTATUS),0)
$(error tools/checks/run.py --make failed (make registration))
endif
$(eval $(CHECK_REGISTRATION))
endif
CHECK_PAR_TARGETS ?= $(CHECK_SHARD_TARGETS)
CHECK_NEEDS ?= $(CHECK_ALL_NEEDS)

.PHONY: $(CHECK_NAMES) check-prepare check-par-run
$(CHECK_NAMES): check-%:
	+@$(if $(findstring n,$(filter-out --%,$(firstword $(MAKEFLAGS)))),echo ,)python3 -B tools/checks/run.py --run $@

# all を先に単独で組む (旧と同じ順): all の末尾が中間物を消すので、同じ並列 make で
# sqlite-module-proto などを組むと、組み直し前の vmkernel を読んだ像ができる。
check-prepare:
	+@$(if $(filter all,$(CHECK_NEEDS)),$(MAKE) all,:)
	+@$(if $(strip $(filter-out all,$(CHECK_NEEDS))),$(MAKE) $(filter-out all,$(CHECK_NEEDS)),:)

CHECK_TIMES ?= build/out/check_times.tsv
.PHONY: check-times-init $(addprefix check-time-,$(CHECK_NAMES))
check-times-init:
	@python3 tools/check_time.py --reset "$(CHECK_TIMES)"

# 準備に失敗したら計時子を起動しない。子は指定した本体だけを実行する。
check-par: check-times-init check-prepare
	+@$(MAKE) check-par-run
check-par-run: $(addprefix check-time-,$(CHECK_PAR_TARGETS))
# Needs finish before their consumers (including the shared AST cache warmup).
$(foreach edge,$(CHECK_EDGES),$(eval $(subst :, : ,$(edge))))
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
