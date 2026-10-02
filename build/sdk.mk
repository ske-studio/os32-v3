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

# 手書きされた KAPI バージョンが kapi.json とずれていないか検査する。
# ずれていると「どれが本当の版か」が分からなくなる。
check-kapi-version:
	@python3 tools/check_kapi_version.py

# KAPI の出力ポインタ宣言 (sdk/kapi.json の "out") と、そこから生成される
# 書き込み可検査 (票 docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md 受入 G1)。
# 見るのは 2 つ: **非 const のポインタ引数があるのに "out" が無いエントリで
# 生成が落ちること** (書き忘れがそのまま穴になるため) と、len / size / unit の
# 解釈と検査順 (**全範囲を検査してから target を呼ぶ**)。合成した kapi.json を
# 一時ディレクトリで回すので実物の生成物には触らない (check-par で回せる)。
# 否定側は `python3 -B tools/tests/test_kapi_out.py --mutate` (生成器を書き換えて
# 戻すので check では回さない)。記録は tools/tests/kapi_out_tdd.md。
check-kapi-out:
	@python3 -B tools/tests/test_kapi_out.py

# 配備マニフェストと app.conf の参照先を検査する (要 make all)
check-manifests:
	@python3 tools/check_manifests.py

# プロジェクト制約 (docs/CONSTRAINTS.md) と、それを参照する CLAUDE.md /
# SOUL.md のずれを検査する。ID での照合なので文言は場所ごとに変えてよい。
check-constraints:
	@python3 tools/check_constraints.py

# [C5] 物理ポインタ変換の走査と、写しへの既知の違反4形の注入。
check-p2v:
	python3 tools/check_p2v.py
	python3 tools/tests/test_p2v.py $(MUT)

# 言語モードの検査 ([C1]、票 docs/archive/v3/TASK_C11_MIGRATION.md §6 段 4)。
# check-constraints は ID の整合だけなので、こちらが**実際の旗とコンパイル結果**を見る:
# make -n -B all のコンパイル行ごとに効いている言語モード (本体・ブート・userland は
# gnu11、SQLite 系は gnu89)、暗黙宣言・暗黙 int・VLA・偽の STATIC_ASSERT の拒否、
# 公開 SDK ヘッダを gnu89 と gnu11 の両方で取り込めること (C99 以降の構文の混入)、
# 配布ライブラリヘッダと gnu89 の例 (sdk/example/hello)、内部実装の AST 型・宣言・include。
# 要クロスコンパイラ (ビルドは要らない)。
check-c-dialect:
	@python3 -B tools/check_c_dialect.py

# check-c-dialect の検査器の試験。--mutate は否定側: 実物の木の写し
# (tools/tests/mutpar.py) に変異 (VLA・暗黙宣言・偽の _Static_assert・SDK ヘッダへの
# C11 構文ほか) を当てて検査器が落ちることを見る。記録は tools/tests/c_dialect_tdd.md。
check-c-dialect-host:
	python3 -B tools/tests/test_c_dialect.py $(MUT)

# ユーザランドの特権命令検査 (リング3 準備)。既定は警告のみ (exit 0) で
# green ビルドを壊さない。リング3 導入後に --strict でゲートする。
check-privileged:
	@python3 tools/check_privileged.py

# カーネル側 C ソースの hlt / cli / sti 直書き検査 (移植性の準備、順序 2)。
# これらは include/io.h の原始命令 (_halt / _idle / _stop / _enable /
# _disable / irq_save / irq_restore) 経由で使い、arch 差し替えの境界を
# io.h 1 枚に閉じ込める。切り出せない箇所は asm の直前に ARCH-ASM-OK と
# 理由を書く。
check-arch-asm:
	@python3 tools/check_arch_asm.py
	@python3 tools/tests/test_clang_ast.py $(MUT)

# 外部形式 (LE) の直アクセス検査 (移植性の準備、順序 4-a)。媒体・書庫の上の
# バイト列は include/endian_le.h の le16_rd / le16_wr / le32_rd / le32_wr を
# 通す。`*(u32 *)&buf[off]` は「x86 は LE」「x86 は非アラインを許す」の 2 つに
# 同時に寄りかかる書き方で、ARM では落ち、BE では値が化ける。
# 実ビルドの旗で libclang の canonical type と alignment を見る (解析失敗は NG)。
check-le-access:
	@python3 tools/check_le_access.py

# GUI 共有プロトコル (os32_gui_shared.h ⇄ proto.rs) の定数・構造体の照合。
# GUI v1.2 の G0 (契約凍結) のゲート。PM 所有 (docs/tasks/gui/v12/TASKS.md §5)。
check-gui-proto:
	@python3 tools/check_gui_proto.py

# 独立端末モデルのホスト試験。guest用Cargo設定を避けるためrootから実行。
# ゲストクロスリンク・描画・CUI統合の検証ではない。
check-term-model:
	cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
	cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline

# 純粋描画アダプタ。人工glyphによるホスト試験であり実ROM描画の検証ではない。
check-term-render:
	cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
	cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline

# T5aの純粋状態・座標・所有権試験。guest.rsの実行は含まない。
check-t5a-host:
	cargo test --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline

# K5a (4 アプリ、契約 T2a) の設計をホストの純粋状態機械で固定したもの。カーネル実装の
# 正しさは何も言わない (実装は K5b)。docs/archive/gui_v13/TASK_K5_multiapp.md §設計。
check-multiapp-model-host:
	python3 -B tools/tests/test_multiapp_model.py
	python3 -B tools/tests/test_multiapp_impl.py
	python3 -B tools/tests/test_owner_reclaim.py

check-memory-host:
	python3 -B tools/tests/test_physmem.py
	python3 -B tools/tests/test_paging_bounds.py $(MUT)
	python3 -B tools/tests/test_app_band_pde.py $(MUT)
	python3 -B tools/tests/test_pgalloc_model.py
	python3 -B tools/tests/test_pgalloc_range.py
	python3 -B tools/tests/test_highram_stage.py
	python3 -B tools/tests/test_memory_boot.py $(MUT)
	python3 -B tools/tests/test_ledger.py $(MUT)
	python3 -B tools/tests/test_lease.py $(MUT)
	python3 -B tools/tests/test_exec_r1.py $(MUT)
	python3 -B tools/tests/test_device_reservation.py $(MUT)
	python3 -B tools/tests/test_sbrk_tier.py $(MUT)
	python3 -B tools/tests/test_app_bb_overlap.py $(MUT)
	python3 -B tools/tests/test_gfx_boot.py $(MUT)

# 記録: tools/tests/memmap_tdd.md (票 docs/archive/kernel_v21/TASK_KSTACK_USER.md)
#
#   check-memmap       実ツリーの地図を検査する。帯どうしの重なり・範囲の逆転・
#                      カーネル本体の予算超過・memmap.h の値を写している場所
#                      (build/os32.ld / kernel/kentry.asm / SDK) のずれ・
#                      docs/02_memory.md の鮮度。**kernel.map が要る**ので
#                      カーネルを組んでいないと 2 で止まる。
#   check-memmap-host  合成した地図で道具と自己診断の挙動を見る。実ツリーの
#                      番地に依存しないので、番地を動かしても腐らない。
# 2026-09-17 (決裁 D1/D2) の配置で両方とも緑になったので check: の列に入れた。
check-memmap-host:
	python3 -B tools/tests/test_memmap_gen.py $(MUT)
	python3 -B tools/tests/test_memmap_boot.py $(MUT)

check-memmap:
	python3 tools/gen_memmap.py --check

check-boot-splash-host:
	python3 -B tools/tests/test_boot_splash_native.py

# NP21/W の停止・起動 (tools/np21w_ctl.py) の変異まで回す。ケースだけなら
# check-tools-host の discover でも回る。
check-np21w-ctl-host:
	python3 -B tools/tests/test_np21w_ctl.py $(MUT)

# NP21/W の ini ライブ変更 (tools/np21w_ini_live.py) の変異まで回す。ケースだけなら
# check-tools-host の discover でも回る (票: tools/tests/np21w_ini_live_tdd.md)。
check-np21w-ini-live-host:
	python3 -B tools/tests/test_np21w_ini_live.py $(MUT)

check-tools-host:
	python3 -B -m unittest discover -s tools/tests -p 'test_np21w_*.py'
	python3 -B tools/tests/test_nhd_deploy_failure.py
	python3 -B tools/tests/test_filer_normalize.py
	python3 -B tools/tests/test_filer_copy_abort.py
	python3 -B tools/tests/test_about_info.py $(MUT)
	python3 -B tools/tests/test_gui_button_dispatch.py
	PYTHONPATH=. python3 -B tools/tests/test_emu_playbook.py
	python3 -B tools/tests/test_mk_settings_db.py
	python3 -B tools/tests/test_fetch_fonts.py
	python3 -B tools/tests/test_mk_blank_nhd.py
	python3 -B tools/tests/test_stat_cmd.py
	python3 -B tools/tests/test_tar_cmd.py

# gshell の入力部・WM をホスト ABI の代用 (host/mocks.rs) で走らせる。
# --mutate は票 KBD_NAV の K1 の変異 (host/integration.py の MUTATIONS) を
# 一時の写しに当てて RED を確かめる (ソースは書き換えない)。
check-gshell-host:
	python3 userland/gshell/host/integration.py $(MUT)

check-db-owned-host:
	python3 -B -m unittest discover -s tools/tests -p 'test_kapi_db_owned.py'

check-vfs-fd-sqlite-host:
	python3 tools/tests/test_vfs_fd_sqlite.py

# FDC のシーク判定 (drivers/fdc_decide.c)。実機 PC-9821Ra266 の FD 起動が
# MOUNT... の root panic になっていた件。見るのは **エミュレータでは踏めない**
# 2 つの分岐: pending 無しの SENSE INTERRUPT STATUS が ST0 だけの 1 バイト
# 応答になること (2 バイト読むと来ないバイトを空転して待つ) と、RECALIBRATE の
# EC (77 ステップでトラック 0 に届かない) が失敗ではなく再試行の合図であること。
# --target はカーネルと同じ i386-elf で drivers/fdc.c ごと通す。
# 記録は tools/tests/fdc_seek_tdd.md。
check-fdc-seek-host:
	python3 -B tools/tests/test_fdc_seek.py --target $(MUT)

# FD のトラック単位の読み出し (drivers/fdc_track.c) と、同じシリンダでの
# シークの省略・まとめ読み (drivers/fdc.c)。実機 PC-9821Ra266 で既定フォント
# (188KB) の読み込みが 1 分以上止まった件 (1 セクタごとに SEEK + ほぼ 1 回転)。
# 本物の fdc.c を µPD765A の模型 (tools/tests/fdc_hostshim/io.h) の上で回す。
# 変異は一時の木の写しに当てるのでソースは書き換えない。
# 記録は tools/tests/fdc_track_tdd.md。
# font_replay (実物のフォントの読み方の再現) は FD イメージを読む。無いまま
# SKIP で通らないように --require-image で FAIL にする (ラリー 2 の Fable)。
# **イメージを前提 (prerequisite) にはしない** — d88 の規則は phony の boot に
# 依るので毎回作り直され、そのたびに kernel も組み直される (kapi_sys.o の
# __DATE__ __TIME__)。check-par の中でそれが起きると vmkernel.lz4 と 1.44MB の
# イメージ・ISO が食い違い、check-packages-host / check-vk32-crc-host が落ちた
# (2026-09-24、PM 判断で「無ければ失敗」に)。make all が先。
check-fdc-track-host:
	python3 -B tools/tests/test_fdc_track.py --target $(MUT) --require-image

# CD の読み (drivers/atapi.c の複数セクタの READ(10)、fs/iso9660.c の覚えた
# パス・先読みの窓・ディレクトリの LRU、userland/lib/rt/pkg.c の区切り)。
# 実機 PC-9821Ra266 の cdinst が 20KB/s を切った件 (READ(10) を 1 セクタずつ、
# VFS の 4KB の区切りごとに根のディレクトリを読み直していた)。
# 本物の atapi.c を ATAPI デバイスの模型 (tools/tests/atapi_hostshim/io.h) の
# 上で回す。変異は一時の木の写しに当てるのでソースは書き換えない (check-par)。
# 記録は tools/tests/cd_read_tdd.md。
check-cd-read-host:
	python3 -B tools/tests/test_cd_read.py --target $(MUT)

# PCI コンフィギュレーションの復号 (drivers/pci_decode.c)。実機 PC-9821Ra266 の
# 内蔵 LAN (Intel 82557) を `lspci` で見つけるための土台 (票 TASK_LAN_82557 L-A)。
# **NP21/W は PCI を実装していない** (`0CF8h` が無い) ので、ここはエミュレータ
# では 1 ビットも踏めない — 走らせて確かめられるのは実機だけで、その 1 回は
# シリアル 115200 での会話。読み違いは全部ここで潰す。
# 見るのは 6 つ: 0CF8h のアドレス語の組み立て (欄をマスクしないと範囲外が隣の
# 欄へ溢れて別のデバイスを読む)、有無を探る値が**本物が保持できる形**であること
# (bit1〜0 を立てると PCI があっても「無い」と答える)、BAR の種別
# (**生値 1 = 番地未割り当ての I/O BAR は「無い」ではない** — 票 R1 が消える)、
# 番地のマスク (I/O は ~3。~0xF で切ると 0xE808 が 0xE800 に化ける)、
# Header Type の bit7 (落とさないとマルチファンクションのブリッヂを見落とす)、
# DWORD からの 8/16 ビットの切り出し。
# --target はカーネルと同じ i386-elf で drivers/pci.c ごと通す
# (列挙は I/O を触るのでホストでは回せないが、型のずれは手元で捕まえる)。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/pci_decode_tdd.md。
check-pci-decode-host:
	python3 -B tools/tests/test_pci_decode.py --target $(MUT)

# シリアルの速度判定 (drivers/serial_plan.c)。実機 PC-9821Ra266 との会話が
# シリアルしかなく、9600 で 490B/s しか出ていなかった件 (票 TASK_SERIAL_VFAST)。
# **NP21/W は通信速度を模擬しない**ので、ここはエミュレータでは踏めない。
# 見るのは 4 つ: V･FAST の速度→分周表 (013Ah bit3-0)、8253 の整数分周
# (1.9968MHz の 38400 は 41600 に化ける / 2.4576MHz なら count 4)、
# TxRDY を待つ予算 (2 × 10 ビット ÷ baud — 0 にすると直す前の hlt 待ちに戻る)、
# **互換 0032h と FIFO 0132h のビット位置の違い** (RxRDY が bit1 と bit2、
# 0x04 は互換では TxEMP なので取り違えても落ちずに静かに壊れる)。
# --target はカーネルと同じ i386-elf で drivers/serial.c ごと通す。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/serial_vfast_tdd.md。
check-serial-vfast-host:
	python3 -B tools/tests/test_serial_vfast.py --target $(MUT)

# 8255 ポート C (0035h) を丸ごと書かないこと (drivers/serial.c)。実機
# PC-9821Ra266 で rshell 中にビープが鳴り続けた件 (2026-09-24、
# docs/POLICY_DEBUG.md §4-59)。0035h は RS-232C の割り込み許可 bit0-2 と
# BUZ (bit3、0 = 鳴動)・SHUT0/SHUT1 などが同じバイトにいるので、許可は
# 0037h の BSR で 1 ビットずつ書く。BUZ の極性は UNDOCUMENTED (07h = 停止)。
# 偽の outp で 8255 を模型にし、初期化と ISR が 0035h を書かないこと・
# 上位 5 ビットが保たれること・ISR が RXRE を落として戻すこと (汲み残し時に
# IRQ4 の立ち上がりが出ること)・初期化が 1 のビットだけ落とすこと・V･FAST
# 経路・kernel/sys.c の buz_on/buz_off が書く値 (06h/07h) を見る。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
check-serial-portc-host:
	python3 -B tools/tests/test_serial_portc.py $(MUT)

# CPU 校正の止め方と丸め (kernel/cpu_calibrate_math.c)。実機 PC-9821Ra266 の
# シリアルが 9600 でも 38400 でも 1 バイト約 2ms しか出なかった件の真因
# (票 TASK_SERIAL_VFAST 往復 3)。校正ループを 1 周だけ回して tick で割って
# いたので、266MHz では 1 tick にも届かず elapsed 0 → 1 に丸められ、
# loops_per_tick が **実際の 1/7〜1/13** になっていた。その結果
# cpu_delay_us(5) が 0.5µs しか待たず、送信ループが TxRDY を待てずに
# hlt へ落ちていた。
# **NP21/W では踏めない** — 十分に遅いので 1 周で 5 tick を超える。
# 見るのは 3 つ: 速い CPU で 5 tick に届くまで回すこと、遅い CPU (8MHz) が
# 1 周のままであること (退行防止)、PIT が死んでも戻ってくること。
# --target はカーネルと同じ i386-elf で kernel/cpu_calibrate.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/cpu_calibrate_tdd.md。
check-cpu-calibrate-host:
	python3 -B tools/tests/test_cpu_calibrate.py --target $(MUT)

# PIT の分周 (kernel/pit_math.c)。pit_init が 1.9968MHz 決め打ちで割っていて、
# 2.4576MHz 系 (実機 PC-9821Ra266、0000:0501h bit7 = 0) では 100Hz のつもりの
# tick が 123Hz = 8.125ms になっていた件 (票 TASK_HAL_WIRING §1-0)。
# **NP21/W は 1.9968MHz 設定なのでエミュレータでは一度も踏めない**
# (§4-49・§4-51・§4-53 と同じ型)。
# 見るのは 4 つ: 両クロックのリロード値 (19968 / 24576、周期はどちらも 10000µs)、
# 100Hz 以外を**既定へ倒さずに**断ること、未知のクロックで割らないこと、
# period_us の中間積が 32bit で溢れないこと (倍率を `U` で持って、64bit ホスト
# でも溢れが再現するようにしてある)。
# --target はカーネルと同じ i386-elf で kernel/sysclk.c と kernel/idt.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pit_clock_tdd.md。
check-pit-clock-host:
	python3 -B tools/tests/test_pit_clock.py --target $(MUT)

# vmkernel.lz4 の LZ4 高圧縮 (tools/mkvmkernel.py、HC level 12) と展開側 3 実装。
# 票 TASK_SERIAL_HOSTFS 部品 A-1 / TASK_HDD_INSTALL N8 (生成側の上限検査)。
# HDD ローダの boot/lz4_mini.c、lz4 コマンドの lib/lz4.c、FD ローダの
# boot/loader_fat_new.asm の pm_lz4_decode (**ASM を切り出して 32bit で実行**) が、
# build/out の実物と合成データ (延長 255 跨ぎ・offset >= 32768・重なり) を
# バイト一致で展開すること、合計が MAX_IMAGE_SIZE を超えたら生成が失敗すること
# (ちょうどは通り +1 は落ちる) を見る。--real は make all の成果物が要る。
# --target はデコーダをローダと同じ i386-elf-gcc で組む。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/vmkernel_lz4_tdd.md。
check-vmkernel-lz4-host:
	python3 -B tools/tests/test_vmkernel_lz4.py --real --target $(MUT)

# VK32 v2 (CRC32 表 + 完全長) の生成と両ローダの検査。票 TASK_SERIAL_HOSTFS 部品 A-4。
# tools/mkvmkernel.py の生成物を zlib.crc32 と突き合わせ、HDD ローダの
# boot/vk32_boot.c (+ lz4_mini.c) と FD ローダの pm_vk32_boot (**ASM を切り出して
# 32bit で実行**) に同じ壊れたイメージ (1 ビット反転・切り詰め・entry_count 0・
# data_offset の範囲外・raw_size の不一致・展開先の範囲外・重なり・壊れた LZ4 列) を
# 渡し、同じ VK32_ERR_* で断ること、展開前に止まるものは窓を 1 バイトも書かないこと
# を見る。FD ローダの FAT チェーン検査 (fat_chain_check、32 ビットの番地で書いた
# 実モードの手続き) も bits 32 で組んで 2HD / 1.44MB の両方で回す。
# --real は make all の成果物 (build/out と images/ の 2HD / 1.44MB) が要る。
# --mutate は写しの上で変異 (組めない変異は ERROR、何も変えない対照を含む)。
# 記録は tools/tests/vk32_crc_tdd.md。
check-vk32-crc-host:
	python3 -B tools/tests/test_vk32_crc.py --real --target $(MUT)

# カーネルに埋め込むコミット ID の生成器 (tools/gen_build_id.py)。一時の git
# リポジトリで clean / -dirty (未追跡は数えない) / unknown / 同じなら書かない を見る。
# 票 TASK_SERIAL_HOSTFS 部品 A-4 (ver の Commit)。記録は tools/tests/vk32_crc_tdd.md。
check-build-id-host:
	python3 -B tools/tests/test_build_id.py $(MUT)

# ブート情報域 0x7E00 (kernel/bootinfo_check.c + boot/bootinfo.inc)。票 TASK_HDD_INSTALL 段 0。
# ローダが INT 1Bh AH=84h の結果を書き、kernel_main が最初に写す。見るのは
# magic / 反転チェック語 (書きかけ) / 版 / ドライブ記録の和 / BX=512・CX/DH/DL≠0・
# CF=0・問い合わせ済み・ローダの valid の規則と、[hdd] 行の書式、
# **NASM 側の写し boot/bootinfo.inc の値が include/bootinfo.h と名前ごとに一致**すること。
# --target は i386-elf で bootinfo.c (構造体の並びの STATIC_ASSERT) と ide.c を通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/bootinfo_tdd.md。
check-bootinfo-host:
	python3 -B tools/tests/test_bootinfo.py --target $(MUT)

# HDD の一時置き場 (票 TASK_HDD_INSTALL 段 1)。区画表の共有部 (drivers/pc98pt.c、
# 標準配置)、ATA の LBA28 / 現在の CHS / 既定の CHS の選択と範囲検査
# (drivers/ide_addr.c)、format の大きさの固定点 (fs/ext2_layout.c)、hdprep の
# 断る条件 (userland/shell/hdprep_plan.c)、ext2_find_partition の失敗と
# ext2_format_at の範囲 (fs/ext2_super.c / ext2_fmt.c、像は e2fsck -fn)、
# Python 側の書き手 (tools/pc98pt.py) と migrate-pt (旧配置の NHD を作って変換
# → e2fsck -fn clean、開始 LBA 不変)。--mutate は写しの上で変異するので並列で回せる。
# 記録は tools/tests/hdd_stage1_tdd.md。
check-hdd-stage1-host:
	python3 -B tools/tests/test_hdd_stage1.py --target $(MUT)

# HDD インストーラ (票 TASK_HDD_INSTALL 段 2)。cdinst / install の共有部
# (userland/system/inst_disk.c・inst_hdd.c) の判定、実物の cdinst.c と install.c を
# main から (8/17・16/63 の区画表と IPL の幾何、空・再作成・旧配置・未知・2 項目、
# 事前検査の失敗で 1 セクタも書かない、書いた後の失敗は INCOMPLETE)、容量の
# 見積もりと実物の ext2_format_at の像 (dumpe2fs) の一致、boot/ext2_mini.c の
# 508KiB 超を切り詰めない。--mutate は写しの上で変異するので並列で回せる。
# 記録は tools/tests/hdd_stage2_tdd.md。
check-hdd-stage2-host:
	python3 -B tools/tests/test_hdd_stage2.py --target $(MUT)

# 8237 DMA 共通部の算数 (drivers/dma8237_math.c)。票 TASK_HAL_WIRING §1-2。
# 見るのは 4 つ: 64KB バンクまたぎの判定、16MB の壁、TC 後の FFFFh を弾く
# 安定読みの採用規則、**バンクレジスタが等差数列でないこと** (ch0 だけ
# 0027h。式で出すと ch1 のバンクを壊す)。
# **NP21/W は 8237 の折り返しも 16MB の壁も模擬しない**ので、またいだ転送が
# 「たまたま読めて」しまう (§4-51 と同じ型)。断る規則をホストで固定する。
# --target はカーネルと同じ i386-elf で I/O を出す側 (dma8237.c) ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/dma8237_tdd.md。
check-dma8237-host:
	python3 -B tools/tests/test_dma8237.py --target $(MUT)

# DMA プールの表 (kernel/dma_pool_math.c)。票 TASK_HAL_WIRING §1-3。
# 池は 0x2E8000〜0x2F7FFF で **0x2F0000 の 64KB 境界を跨ぐ**ので、跨ぐ候補を
# 飛ばして後半に置けること・33KB が空の池でも必ず失敗すること・隣接 span の
# 解放が先頭一致でだけ通ること・LEAKED を再利用しないことを見る。
# --target は i386-elf で唯一の池 (dma_pool.c) ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/dma_pool_tdd.md。
check-dma-pool-host:
	python3 -B tools/tests/test_dma_pool.py --target $(MUT)

# CS4231 (MATE-X PCM) の再生ドライバ (drivers/pcm_cs4231_math.c + pcm_cs4231.c)。
# 票 docs/tasks/v3/TASK_PCM_CS4231.md §2-3 / E1。
# **NP21/W では踏めない分岐がここの主目的**: 1 周回った観測 (同じ半分で位置が
# 戻る)、補充の余裕 (REFILL_MARGIN) を割った切り替え、drain の 3 段階、初期化列
# と停止列の**順序**、入口ガードでの装置アクセス **0 回**、close / reclaim が
# 各状態から **1 度だけ**解放すること。
# io.h だけ tools/tests/pcm_hostshim/io.h で差し替え、ポートと割り込み禁止を
# 模型へ回す (b8_hostdrv と同じ作法)。
# --target はカーネルと同じ i386-elf で I/O を出す側ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pcm_cs4231_tdd.md。
check-pcm-cs4231-host:
	python3 -B tools/tests/test_pcm_cs4231.py --target $(MUT)

# キーボード 8251 のステータス判定 (drivers/kbd_status.c)。実機 PC-9821Ra266 で
# 本体キーボードの打鍵が一切届かなかった件 (docs/POLICY_DEBUG.md §4-57)。
# IRQ1 ハンドラは 0041h を読む前に 0043h を見て、RxRDY = 0 なら空 IRQ、
# PE/FE なら読み捨て + ER で解除、OE だけなら使って ER で解除、どれでもなければ使う。
# V86 へ IRQ1 を反射するのは実データのときだけ (kernel/v86_kbd.c の空読みは
# 前回のバイト) — 空 IRQ の反射でゲストに偽の ESC が届いていた。
# **EMPTY と ERROR は NP21/W では踏めない** (keyboard_i43 は `status | 0x85`
# を返し、IRQ1 の前に必ず RxRDY を立てる)。逆に `| 0x85` のビット
# (DSR / TxEMP / TxRDY) で DATA にならないとエミュレータの打鍵が全部落ちる。
# --target はカーネルと同じ i386-elf で drivers/kbd.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/kbd_status_tdd.md。
check-kbd-status-host:
	python3 -B tools/tests/test_kbd_status.py --target $(MUT)

# キーボードの受信記録 (drivers/kbd_dlog.c、KAPI v67 kbd_diag_log) と `kbdstat -w`
# の行 (userland/shell/kbd_watch.c)。票 docs/tasks/gui/TASK_KBD_NAV.md §3 — 実機で
# カナ / CAPS が「ロックで make、解除で break」か「押すたびに make だけ」かを見る準備。
# 32 件の循環 (seq の飛び = 取りこぼし)、IRQ1 は EMPTY / ERROR を積まず OVERRUN は
# 印付きで積む、修飾は配った後の値、kbd_diag_log の引数検査、行と LOST 行の書式。
# drivers/kbd.c は IRQ1 ハンドラごと実物を回す (0043h / 0041h は
# tools/tests/kbd_hostshim/io.h の模型)。**EMPTY / ERROR は NP21/W では踏めない**。
# --target はカーネルと同じ i386-elf で 3 本を通す (構造体の大きさの STATIC_ASSERT)。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/kbd_dlog_tdd.md。
check-kbd-dlog-host:
	python3 -B tools/tests/test_kbd_dlog.py --target $(MUT)

# PCI の結線表 (drivers/pci_bind_match.c)。票 TASK_HAL_WIRING §1-4。
# **NP21/W には PCI が無い**ので、この層はエミュレータでは 1 行も走らない。
# 一致規則 (4 欄の AND と「任意」)、DECLINE → 次の候補へ、QUARANTINE →
# **その BDF の探索を打ち切る**、理由が候補ごとに初期化されること、
# 線の様子を**読む時点で合成する** (結線後に隔離されても BOUND のまま
# line_state だけ変わる) を見る。probe は関数ポインタなので偽 driver で足りる。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pci_bind_tdd.md。
check-pci-bind-host:
	python3 -B tools/tests/test_pci_bind.py --target $(MUT)
# 動的 IRQ の判断 (kernel/irq_math.c、票 TASK_HAL_WIRING §1-1)。
# 見るのは **NP21/W でも実機でも狙って作れない**重なり: A と B が同時に要因を
# 持つ (最初の HANDLED で打ち切らない)、1 巡目で受けて 2 巡目が空
# (handled_any を 0 で上書きしない)、1 tick に 200 回と 201 回 (ストームの
# 閾値)、IRQ15 のスレーブ ISR bit7 が落ちている (スプリアスにスレーブ EOI を
# 送らない)、登録の拒否規則 (固定 IRQ / 範囲外 / 重複 / SHARED 不一致 /
# 5 件目 / 隔離済み / ISR 文脈)。
# --target はカーネルと同じ i386-elf で kernel/irq.c ごと通す。
# 記録は tools/tests/irq_math_tdd.md。
check-irq-math-host:
	python3 -B tools/tests/test_irq_math.py --target $(MUT)

# µs 時計の判定と算数 (kernel/time_math.c、票 TASK_HAL_WIRING §1-5)。
# 見るのは p1/p2 の判定表 3 分岐 (実 PIT では呼び出しから p1 読みまでに境界を
# 越える機械があり、位相待ちでは 0/0 と 0/1 を撃ち分けられない) と、
# **71 分の桁あふれ** (tick との積を u32 で組むと 429496 tick で時刻が 0 に
# 戻る。起動から 71 分はエミュレータでも実機でも 1 度も回していない)。
# --target は kernel/ktime.c ごと通す。記録は tools/tests/time_math_tdd.md。
check-time-math-host:
	python3 -B tools/tests/test_time_math.py --target $(MUT)

# `v86 -g` の記録器と引数の判定 (kernel/v86_gcap_math.c、票 TASK_PEGC480_REALHW §3 段 1)。
# 実機の ROM の INT 18h AH=31h/30h を V86 で呼び、その間の OUT を畳まずに積む。
# 見るのは: 通すポートの表 (09A8h / 09A0h / 60h〜7Ah の偶数 / A0h〜A6h)、
# 打ち切る命令 (INS/OUTS と 66h 付きの IN/OUT EAX、バイト形は 66h でも通す)、
# 列の順序・seq (IN も数える)・幅での値の切り方・溢れ (513 件目で overflow、
# 実機へは通し続ける)・IN の表、幅で ops の口 (8 / 16) を選ぶこと、
# AH=31h の値から並び (NP21/W の bit2 / Bible の bit3) と 640x480 の AH=30h を
# 決める表 (**両方・どちらでもない・印のまま**)。**NP21/W でも実機でも狙って
# 踏めない分岐**。ゲストのバイト列が正本の asm と同じかも見る (nasm が要る)。
# --target は i386-elf で kernel/v86_gcap.c ごと通す。
check-v86-gcap-host:
	python3 -B tools/tests/test_v86_gcap.py --target $(MUT)

# ホスト道具 tools/rshell_serial.py の「応答の識別」。実機の rshell と
# 話すときに **EOT の対応が 1 つずれる** 事故を止める (票 TASK_SERIAL_VFAST
# の Codex レビュー往復 3 ⑤⑥)。見るのは 3 つ: エコー行を **行全体** で
# 比べること (`> version` を `ver` の応答と読まない)、`exit` がエコーを
# 出さない例外、`ver` の成功を **本文 (`Build:`) + エコー + EOT** の 3 つで
# 判定すること (先行コマンドの EOT を拾っただけで成功と読まない)。
# **タイミングで起きる事故なので実機では「たまたま通る」** — 規則そのものを
# ここで固定する。pyserial もシリアルポートも要らない。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/serial_vfast_tdd.md。
check-rshell-serial-host:
	python3 -B tools/tests/test_rshell_serial.py $(MUT)

# シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B、受入 T1 と T5 のホスト部分)。
# ゲスト側の実物 (fs/sfs_proto.c・fs/sfs_client.c・fs/serialfs.c、drivers/serial.c の
# ゲート、rshell の ESC と `sfs run` の行、hsync の vmkernel.old の判定) を偽の線と
# 仮想の時計で、ホスト側の実物 (tools/serialfs_host.py・tools/rshell_serial.py) を
# 偽のポートで回し、C と Python を相互に照合したうえで実時間のパイプで結合する。
# 障害の注入: 応答の喪失・遅延 (番号のずれ)・CRC 破損・番号 / セッション違い・
# ホストの停止・ごみの連続・再送で副作用が二重にならない・セッション中に rshell へ
# フレームが漏れない・`sfs run` の行の外で `cat` の本文のフレームに答えない。
# --target はカーネルと同じ i386-elf -Werror。--mutate は写しの上で変異 (組めない
# 変異は ERROR、恒等の対照を C と Python に 1 本ずつ)。記録は tools/tests/serialfs_tdd.md。
check-serialfs-host:
	python3 -B tools/tests/test_serialfs.py --target $(MUT)

# kprintf の属性変換 (lib/kprintf_attr.c)。呼び出し側の 70 か所以上が渡す
# PC/AT (CGA) 流の 0x07 などを、PC-98 のテキスト属性 (bit0 = 表示 /
# bit5,6,7 = 色) へ直す。直さないと属性 VRAM へ「色無し + リバース +
# ブリンク」が書かれ、**診断行が画面に 1 文字も出ない** (実機
# PC-9821Ra266 の FD 起動を追えなかった原因、2026-09-22)。
# **エミュレータでは踏めない** — /api/tvram は文字コードしか返さないので、
# 属性が壊れていても「出ている」ように読める。
# --target はカーネルと同じ i386-elf で lib/kprintf.c ごと通す。
# 記録は tools/tests/kprintf_attr_tdd.md。
check-kprintf-attr-host:
	python3 -B tools/tests/test_kprintf_attr.py --target $(MUT)

# gfx/backend_cirrus.c の窓の可否 (cirrus_win_usable)。RAM の上端ではなく
# 物理地図 (pgalloc_range_has_ram) で決める — 15MB + 高位 RAM の構成で穴の中の
# バンク窓まで拒んで probe が落ちていた (POLICY_DEBUG §4-34 と同じ形)。
# 実物のソースをホスト ILP32 で回し、i386-elf -Werror でも通す。
# 記録は tools/tests/cirrus_win_tdd.md。
check-cirrus-win-host:
	python3 -B tools/tests/test_cirrus_win.py $(MUT)

# gfx/backend_pegc.c の 640x480 へ入る / 戻る OUT 列と GDC の FIFO 待ち
# (票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §2 H2・H3・H5、§4)。実物の
# ソースをホスト ILP32 で回し、偽の I/O で ポート・値・順序を期待列と比べる。
# 待ちの上限 (詰まったままでも終わる) も見る。i386-elf -Werror でも通す。
# 記録は tools/tests/pegc_mode_tdd.md。
check-pegc-mode-host:
	python3 -B tools/tests/test_pegc_mode.py $(MUT)

# fs/vfs.c + fs/ext2_vfs.c の mount 経路。fd0 が hd0 に化けて同じ
# パーティションを二重マウントする回帰 (2026-09-10) を止める。
check-vfs-mount-dev-host:
	python3 -B tools/tests/test_vfs_mount_dev.py
	python3 -B tools/tests/test_ext2_read_bound.py
	python3 -B tools/tests/test_ext2_write_io.py
	python3 -B tools/tests/test_fatfs_stat.py

check-sqlite-groups-host:
	python3 tools/tests/test_sqlite_groups.py

# kernel/con_sink.c のリング (票 K6C)。実物のソースをホスト ILP32 で走らせ、
# 同じソースが i386-elf-gcc -Werror でも通ることを見る。記録は
# tools/tests/con_sink_tdd.md。
check-con-sink-host:
	python3 -B tools/tests/test_con_sink.py

# kernel/bootlog.c — 最後の起動のログ (/var/log/boot.log)。実物のソースをホストで
# 走らせ (溜める・あふれ・組み立て・書き出しの手順を偽の VFS で)、変異を当て、
# 同じソースが i386-elf-gcc -Werror でも通ることを見る。記録は
# tools/tests/bootlog_tdd.md。
check-bootlog-host:
	python3 -B tools/tests/test_bootlog.py --target $(MUT)

# kernel/kbd_inject.c の 256B リング (票 K7)。実物のソースを kernel/con_sink.c と
# 同じ翻訳単位で走らせ (注入の権限は con_sink の読み手 1 本)、同じソースが
# i386-elf-gcc -Werror でも通ることを見る。記録は tools/tests/k7_tdd.md。
check-kbd-inject-host:
	python3 -B tools/tests/test_kbd_inject.py

# exec/launch.c の起動要求表 (票 T9 D3)。実物のソースを exec/appslot.c と同じ
# 翻訳単位で走らせ (要求者・子の所有・token の照合は AppSlot を引く)、同じ
# ソースが i386-elf-gcc -Werror でも通ることを見る。記録は tools/tests/t9_tdd.md。
check-launch-host:
	python3 -B tools/tests/test_launch.py

# exec/ring3_str.c — KAPI が CPL=3 へ **返す** 文字列の置き場 (票 T9 §12 R1)。
# カーネル帯には USER ビットが無いので、sys_getcwd がそのまま返すと CPL=3 の
# 呼び手が #PF で畳まれる。写し先 (トランポリンページの空き) の番地の式と
# 経路の分岐をホストで踏む。記録は tools/tests/t9_tdd.md。
check-ring3-str-host:
	python3 -B tools/tests/test_ring3_str.py

# exec/ring3_str.c の ring3_guard_active と kernel/gui.c の入口の配線 (2026-09-26、
# filer が窓も出さずに消えた件)。WM はアプリの syscall の中で走るので、
# ring3_in_syscall だけで「アプリ由来」と決めると WM 自身のポインタが KAPI の
# 出力検査で拒否され、アプリが kill される。判定表と gui_call / owner_exit の
# 前後の対をホストで踏み、--mutate は写しの上で変異するので並列 (check-par) で
# 回せる。記録は tools/tests/ring3_guard_tdd.md。
check-ring3-guard-host:
	python3 -B tools/tests/test_ring3_guard.py --target
	python3 -B tools/tests/test_ring3_guard.py --mutate

# userland/shell/sh_launch.inc の起動待ち (票 T9 D3a)。実物のソースを
# tools/tests/sh_launch_host.c がそのまま #include し、KernelAPI の
# launch_req / launch_poll / sys_yield / kprintf を差し替えて DONE / FAILED /
# STALE / FULL の 4 経路と「待ちの間 kbd_* / ime_* を呼ばない」を見る。
# 同じソースが i386-elf-gcc -Werror でも通ることも別に見る。記録は t9_tdd.md。
check-sh-launch-host:
	python3 -B tools/tests/test_sh_launch.py

# userland/shell/sh_redraw.inc の行再描画 (実装レビュー blocker 1 — GUI 中は
# コンソール座標が動かない) と userland/shell/cmd_script.c の source 中 exit
# (blocker 2 / D2(d))。どちらも実物のソースを tools/tests/sh_shell_host.c が
# そのまま #include する。記録は tools/tests/t9_tdd.md。
check-sh-shell-host:
	python3 -B tools/tests/test_sh_shell.py

# シェルが入力を黙って切り詰める経路 (票 docs/archive/shell/TASK_SH_TRUNCATION.md)。
# tools/tests/sh_truncation_host.c が userland/shell/main.c を丸ごと #include し、
# **登録表も execute_command も実物のまま**回す (sh_shell_host.c は
# execute_command をスタブにしているのでこの経路を試験できない)。
# 段 2 の時点で T1 (`if` の比較) と §2-1 (スクリプト中で断ったら打ち切る) が
# 入っている。否定側は `--mutate` (印を立てない / 消し忘れる / 対話でも
# 打ち切る 版などが RED になる)。記録は tools/tests/sh_truncation_tdd.md。
check-sh-truncation-host:
	python3 -B tools/tests/test_sh_truncation.py

# 終了コードの配線と `$?` (票 docs/archive/shell/TASK_EXIT_STATUS.md)。
# tools/tests/sh_status_host.c が userland/shell/main.c を丸ごと #include し、
# **登録表も execute_command も実物のまま**回す (受入 R2)。同じ 1 本を
# **2 通り** — 常駐 (exec_run + exec_last_result、KAPI v55) と
# -DSHELL_AS_APP (要求表経由) — にコンパイルして両方走らせる。種別ごとの
# 写像はビルドで実装が違うので、片方だけでは配線を見たことにならない。
# 否定側は `--mutate` (種別を値から作る / PATH 走査を値で止める /
# `exit` の値を捨てる / `set -e` を拾わない 版などが RED になる)。
# 記録は tools/tests/sh_status_tdd.md。
check-sh-status-host:
	python3 -B tools/tests/test_sh_status.py $(MUT)

# 通常配備が /etc/settings.db* を作らない・上書きしない・消さない (票 S0-D / D0)。
# temp dir + mock だけで走り、sudo / mount / 実配備は試験側が遮断する。記録は tools/tests/s0_tdd.md 節 D。
check-settings-protect-host:
	python3 -B tools/tests/test_deploy_protect.py
	python3 -B tools/tests/test_hsync_protect.py

# hsync の同サイズ更新の検出 (票 H1、docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md
# §9 の A01〜A13)。実物の userland/system/hsync.c を #include し、KernelAPI だけを
# オンメモリの贋 FS に差し替えて回す。fs/hostdrv_stat_rules.inc (HostDrv の stat
# 失敗の是正) と CRC ストリーム核の既知ベクトルも同じ翻訳単位で見る。
# 記録は tools/tests/h1_tdd.md。
check-hsync-h1-host:
	python3 -B tools/tests/test_hsync_h1.py --target

# hsync の mtime 取得・保存と日時の前置判定 (票 H3、docs/archive/shell/TASK_H3.md
# §6 / §8)。H1 と同じく実物の userland/system/hsync.c を #include し、贋 FS に
# **ノードごとの mtime** と sys_set_mtime (成功 / NOSYS / I/O 失敗) を持たせて回す。
# FILETIME (1601 起点・100ns) -> Unix 秒の境界 (A16) は fs/hostdrv_stat_rules.inc の
# 純関数を直接叩き、vfs_set_mtime の NOSYS 振り分けは実物の fs/vfs.c で見る。
# --mutate は否定側 (日時が不明なのに省略する版などで落ちることの確認)。
# 記録は tools/tests/h3_tdd.md。
check-hsync-h3-host:
	python3 -B tools/tests/test_hsync_h3.py --target $(MUT)

# 排他的作成 O_EXCL (票 H2 §2-1、KAPI v53)。実物の fs/vfs.c + fs/vfs_fd.c を
# #include し、create_excl を**持つ / 持たない**合成 VfsOps で、既存 (ファイル /
# ディレクトリ) が EXIST、判定不能がその負値、O_EXCL 単独が INVAL、非対応 FS が
# NOSYS になることを見る。**非対応の判定が種別検査より先**であることは
# get_file_size / list_dir の呼び出し回数 0 で押さえる。
# --mutate は否定側 (読めなかったを無いと読む版 / 非対応の判定を後ろへ動かした版)。
# 記録は tools/tests/vfs_excl_tdd.md。
check-vfs-excl-host:
	python3 -B tools/tests/test_vfs_excl.py --target $(MUT)

# 配備マニフェストと世代の確認 (票 H4、docs/archive/shell/TASK_H4.md §2-1〜§2-3)。
# **読む側**は H1 / H2 / H3 と同じく実物の userland/system/hsync.c を #include し、
# 贋 FS の /host/.deploy/manifest.txt に票が挙げた壊し方を注入して回す。
# 名札が無い配備元が今までどおり動くこと、壊れた名札を捨てること、断るのが
# `--expect-build` かつ全体同期のときだけであること、**名札が読めないことを
# 「一致」と扱わない**こと、名札を信じて内容比較を省かないことを見る。
# format=2 (票 TASK_KAPI_DATA_FIELDS) で名札に kapi= / kapi_version= が入り、
# 配置違い・版が新しい・確かめられない名札は既定で断る (KAPI の門、case_kapi)。
# 既存の H4 の段は `--force-kapi` を付けて回す。
# **書く側** (tools/hostdrv_deploy.py) は一時ディレクトリだけで回し、全件成功の
# 後にだけ書くこと・失敗したら既にある名札を消すこと・一時ファイル + 置き換え・
# --no-manifest を見る。両層が同じ名札を指していることは静的に突き合わせる。
# --mutate は否定側 (壊れた名札を一致と扱う版 / 名札の CRC で比較を省く版など)。
# 記録は tools/tests/h4_manifest_tdd.md。
check-h4-manifest-host:
	python3 -B tools/tests/test_h4_manifest.py --target $(MUT)
	python3 -B tools/tests/test_hostdrv_manifest.py $(MUT)

# KAPI データ欄の固定配置と OS32X ヘッダ v3 (票 docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md)。
# exec / shlib ローダ / 常駐シェルの判定関数 (exec/os32x_hdr.c を
# tools/tests/os32x_layout_host.c が #include)、gen_kapi.py の容量拒否、
# mkos32x.py / mkshlib.py のヘッダ v3 (値 = ELF の .os32_kapi_layout、刻印が
# 無ければ失敗、min_api_ver >= 63)、crt の kapi 改名で作り直し忘れの .o が
# リンクで落ちること。--mutate は判定・拒否を崩した版で落ちることを見る
# (変異は一時ディレクトリの写しの木に当てるので並列で回せる)。i386-elf の道具を使う。
check-kapi-layout-host:
	python3 -B tools/tests/test_kapi_layout.py $(MUT)

# hsync の置換安全化 (票 H2、docs/archive/shell/TASK_H2.md §2-3 / §2-4)。H1 / H3 と
# 同じく実物の userland/system/hsync.c を #include し、贋 FS に O_EXCL /
# sys_rename (公開の前に失敗 / 公開の後に失敗 / 判定不能) / ROFS / st_nlink /
# api->version を持たせて回す。公開の判定が **st_ino** であること、NOSYS で
# 直接上書きへ落ちないこと、予約名 .hs~ の掃除が st_nlink を見ないこと、
# 保護が予約より優先されること、KAPI v53 未満を既定で断ることを見る。
# 記録は tools/tests/hsync_h2_tdd.md。
check-hsync-h2-host:
	python3 -B tools/tests/test_hsync_h2.py --target $(MUT)

# hdrv_list_dir の列挙ループ (票 H1 の「I/O 失敗を成功にしない」/ 対象
# 「HostDrv のエラー処理」)。実物の fs/hostdrv_list_rules.inc を #include し、
# hostdrv_query_dir に当たる 1 件取得だけを贋物にして、(a) 途中で負値 /
# (b) 件数上限での打ち切り が VFS_OK で返らないことを見る。
# 記録は tools/tests/h1_tdd.md。
check-hostdrv-list-host:
	python3 -B tools/tests/test_hostdrv_list.py --target

# シェルの種別判定と cp -r の宛先階層 (票 H1 / 往復 3 の B5)。実物の
# cmd_fs_shared.c + cmd_file.c を #include し、sys_stat は正しいまま sys_ls だけを
# FULL / IO にして、列挙のエラーが「ディレクトリでない」に化けないことを見る。
check-fs-kind-host:
	python3 -B tools/tests/test_fs_kind.py --target

# 種別が「分からない」とき cp / mv / rm が断る (TASK_FS_TYPE §3)。実物の
# cmd_fs_shared.c + cmd_file.c を #include し、sys_stat と sys_ls の両方を IO にして、
# 呼び出し元 5 箇所が「不明」をファイルと読まず、open / mkdir / rename / unlink を
# 呼ばないことを受け手で見る。§6 は継承バグ台帳の `cp -r` — 失敗する経路で
# 宛先に空のディレクトリを残さない (収集してから mkdir / mkdir の戻り値を見る /
# 既に在るディレクトリへの上書きコピーだけは通す)。--mutate は否定側。
# 記録は tools/tests/fs_kind_callers_tdd.md。
check-fs-kind-callers-host:
	python3 -B tools/tests/test_fs_kind_callers.py --target $(MUT)

# `cat -n` の行番号は行の先頭でだけ出る。実物の cmd_fs_shared.c + cmd_file.c を
# #include し、sys_write(1, ...) に出た全バイトを試験側の素朴な参照実装と 1 バイト
# ずつ突き合わせる。(a) 改行で終わるファイルの後ろに空の行番号を出さない、
# (b) IO_BUF_SIZE (65536) の切れ目で行が終わったことにしない (行頭の状態を
# 読み取りをまたいで持つ) の 2 つ。§6 は継承バグ台帳の「内蔵 cat が標準入力を
# 読まない」— 引数が無ければ FD 0 を読み、FD 0 は閉じず、引数があれば読まない。
# --mutate は否定側で、(a) (b) と §6 の 3 つを壊した版が RED になることを見る。
# 記録は tools/tests/cat_linenum_tdd.md。
check-cat-linenum-host:
	python3 -B tools/tests/test_cat_linenum.py --target $(MUT)

# vfs_path_kind のプローブ (票 H1 / 往復 3 の B6)。実物の fs/vfs.c を #include し、
# 「読めなかったディレクトリ」が get_file_size 経由でファイルに化けないことを見る。
check-vfs-kind-host:
	python3 -B tools/tests/test_vfs_kind.py --target

# fstat がリダイレクトに従う / isatty と食い違わない (票 TASK_FSTAT_REDIR の F4 F5)。
# 実物の fs/vfs.c + fs/vfs_fd.c + **fs/fd_redirect.c** を #include し、リダイレクトは
# 贋物を置かずに実物を通す。`> file` なら fstat が実体 (S_IFREG・大きさ・時刻・inode)
# を答えること、パイプは S_IFIFO であること、そして **fstat が S_IFCHR ⇔ isatty が 1**
# が状態 (コンソール / ファイル / パイプ) × fd 0/1/2 の総当たりで成り立つことを見る。
# 判定の管理元が 1 か所 ([C4] fd_redirect_ifmt) であることは静的に突き合わせる。
# --mutate は否定側 (fstat だけ見ない版 / **isatty だけ見ない版** /
# パイプを S_IFCHR と答える版 / 実体ではなく作り話を返す版)。
# 記録は tools/tests/fstat_redir_tdd.md。
check-fstat-redir-host:
	python3 -B tools/tests/test_fstat_redir.py --target $(MUT)

# 読み取り失敗を「不存在」にしない (票 B8)。実物の ext2 を RAM ディスクへ載せ、
# **間接ブロックを使う大きなディレクトリ**の読み出しを一度だけ落として、
# 実物の vfs_open / vfs_open_sqlite まで通す。O_CREAT が既存ファイルを空に
# しないことを、FD・write_file の呼び出し回数・読み直した中身で押さえる。
# 2 巡目 (Codex 実装レビュー P1-4): 実物の fs/hostdrvfs.c を io.h の差し替えと
# 贋の NP21/W で動かし、OPEN の失敗が NOTFOUND に畳まれないことを見る。
check-b8-open-host:
	python3 -B tools/tests/test_b8_open.py --target
	python3 -B tools/tests/test_b8_hostdrv.py --target

# 長さ 0 の名前を ext2 に載せない (票 TASK_EXT2_EMPTY_NAME)。cdinst の NHD の
# ルートに名前の無いディレクトリ (inode 23) が在り、Linux の e2fsck が壊れと
# 判定した — pkg 展開の mkdir("/hd0") が VFS で "/" になり、ext2 が名前 "" で
# 作っていた。実物の ext2 + vfs.c + vfs_fd.c + userland/lib/rt/pkg.c で
# マウント点・末尾 "/"・"//"・"."・".."・256 文字を通し、cdinst と同じ並びで
# 実物の mkpkg.py の PKG を展開した像を**本物の e2fsck -fn** に当てる (clean 必須)。
# --mutants は fs/ の**写し**を変異させるので実物は書き換えない (並列段に置ける)。
# 記録は tools/tests/ext2_empty_name_tdd.md。
check-ext2-empty-name-host:
	python3 -B tools/tests/test_ext2_empty_name.py --target $(MUTS)

# 票 TASK_VFS_FD_PATH: FD は open 時の inode で読み書きし、unlink・置き換え
# rename・umount で失効する (欠陥 1: 開いた FD の書き込みが同じ名前に作り直した
# ディレクトリを上書きした)。長いパス・深いパスは切り詰めずに断る (欠陥 2)。
# 実物の ext2 + vfs.c + vfs_fd.c + userland/lib/rt/pkg.c で像を e2fsck -fn に当て、
# 実物の mkpkg.py の上限、実物の sdk/crt/syscalls.c の errno (newlib ヘッダ)、
# 実物の kernel/ime_dict.c の開き直し (SQLite 込み) も見る。--mutants は写しを
# 変異させるので実物は書き換えない (並列段に置ける)。
# 記録は tools/tests/vfs_fd_path_tdd.md。
check-vfs-fd-path-host:
	python3 -B tools/tests/test_vfs_fd_path.py --target $(MUTS)

# KAPI v50 (db_open_existing / prepare_only / bind_* / error_code、票 S0-K)。実 SQLite + 実 VFS + RAM backend。
check-db-v50-host:
	python3 -B tools/tests/test_kapi_db_v50.py

# db_last_error / db_column_text の返り先 (票 TASK_DB_ERRSTR)。実 SQLite + 実 kapi_db.c。
# 見るのは「返り先が共有メモリの範囲内か」と「結果を上限まで書いても診断文が壊れないか」。
# --mutate は否定側 (上限の引き算を 1 か所戻す / カーネル番地のまま返す / NUL を置き忘れる)。
check-db-errstr-host:
	python3 -B tools/tests/test_db_errstr.py --target

# libos32cfg / cfg コマンドのホスト TDD (票 S2-C)。実 SQLite + 実 kapi_db.c + RAM backend。
check-cfg-host:
	python3 -B tools/tests/test_cfg.py

# install --recover-settings / --revert-settings のホスト TDD (票 S3-I)。実 SQLite + 実 kapi_db.c + RAM backend。
check-install-recover-host:
	python3 -B tools/tests/test_install_recover.py

# install (無印) の通常インストール経路のホスト TDD (票 S3I2-I)。実 install.c + KAPI の贋物、--target で IdeInfo 96B の表明。
check-install-fresh-host:
	python3 -B tools/tests/test_install_fresh.py --target

# tools/host_agent.py v2 (ワイヤ v2 の Agent 側、票 N1 段 1)。贋 OS32 が
# フレームを直接組んで rid 台帳 / 3 way HELLO / 墓標 / 枯渇停止を踏む。
check-host-agent:
	python3 -B tools/tests/test_host_agent.py
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

# net/link.c (ワイヤ v2) + kapi/kapi_host.c (KAPI v51) のホスト TDD (票 N1 段 4)。
# 実物のソースを #include し、NIC / cli-sti / 100Hz タイマ / ディスパッチャだけを
# 贋物にする。対向は **実 Agent** (host_agent.py を UNIX ソケットで子プロセス起動)
# か台本。記録は tools/tests/n1_tdd.md。
check-net-link-host:
	python3 -B tools/tests/test_net_link.py --target
# libos32gui の os32gui_cfg_* wrapper の分岐 (票 S2-W)。C の実体は贋物。
check-gui-host:
	cargo test --manifest-path userland/rust/libos32gui/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline
# libos32host + wget/lpr/hclip/hdate のホスト TDD (票 N3)。実物のソースを #include し、
# KAPI / libos32host の関数だけを贋物に。記録は tools/tests/n3_tdd.md。
check-host-lib-host:
	python3 -B tools/tests/test_host_lib.py --target

# 試験プログラムの合否を機械が読める形にする約束事 (票 docs/archive/test/
# TASK_TEST_RESULT.md §2 / §11)。終了コード (0 / 1 / 2、予約値 126/127/130/139 は
# 返さない) と最終行の集計行 `<名前>: PASS <n>/<m>` が**必ず一致する**ことと、
# その集計行が **fd 1 に出る**ことを固定する。実物の userland/lib/rt/testresult.h
# と、userland/tests/ の実物のプログラム 5 本 (stat_t / restest / test2 /
# klibc_test / font_load_test) を贋物の KernelAPI で**実際に走らせ**、
# **どの fd に何が書かれたか**と main の返り値の両方を観測する — grep では
# 一致は確かめられないし、画面 (kprintf) と fd 1 を同じバッファへ流す贋物では
# 票 §11 の穴 (16 本中 14 本の集計行がリダイレクトで拾えなかった) を見逃す。
# 静的側は第 1 陣 (票 §4) 16 本の `void main` / 名前が argv[0] 由来 /
# main の return が集計の答えでない / ヘッダの内部 (os32_test__*) を直接呼ぶ、
# を見る。Rust の alloc_demo は書式と終了コードを testresult.h と突き合わせる。
# --mutate は否定側 (どれもコンパイルは通る変異)。
# 記録は tools/tests/result_conv_tdd.md。
check-result-conv-host:
	python3 -B tools/tests/test_result_conv.py --target $(MUT)

# ゲストで一括実行してホストで集計するランナー (票 docs/archive/test/
# TASK_TEST_RUNNER.md)。**2 つのターゲットは別物なので混ぜないこと。**
#
#   check-guest-host  ランナーの**ホスト試験** (受入 R7)。NP21/W に触らない。
#                     生成 (一覧 → 平らなスクリプト) と集計 (出力 → 判定) は
#                     エミュレータにも時計にも触らない純関数に切ってあるので、
#                     贋物の入力だけで全部踏める。**そこが壊れていたらゲストで
#                     回しても意味がない**ので、これは `check` の列に入れる
#                     (変異は tools/guest_tests.py の写しの木に当てる —
#                     tools/tests/mutpar.py、並列で回せる)。
#                     --mutate の否定側: 食い違い (0 なのに FAIL) の見逃し /
#                     見張りが発火しない / 固まった試験を名指ししない /
#                     /host が無いのに合格にする / 落ちた試験 (139) で後続を
#                     打ち切る / 前回の出力が混ざる (R8)。
#                     記録は tools/tests/guest_tests_tdd.md。
#
#   check-guest       **本番。ゲストで実際に走らせる。**`make check` の列には
#                     入れない — NP21/W が動いている必要があり ([D1] の領域)、
#                     `make check` はホストだけで完結する約束だから (票 §4)。
#                     走らせる一覧は tools/tests/guest_tests.txt。
check-guest-host:
	python3 -B tools/tests/test_guest_tests.py $(MUT)

check-guest:
	python3 tools/guest_tests.py

# kstring の C 版 (移植性準備の順序 4-b)。実物の lib/kstring_asm.asm (nasm) と
# 実物の lib/kstring_c.c を **同じ実行ファイルにリンク**し、13 本すべてを同じ
# 入力で突き合わせる (戻り値とバッファの全内容が一致すること)。x86 の既定
# ビルドはアセンブリのままなので、これは切り替えの門ではなく答え合わせ。
# --mutate は否定側 (kstrcmp を符号付きにすると日本語ファイル名の並び順が
# アセンブリ版と食い違って落ちる、など)。記録は tools/tests/kstring_c_host.c。
check-kstring-c-host:
	python3 -B tools/tests/test_kstring_c.py $(MUT)

# kstring の実測プログラム kstr_bench の**計測の枠組み** (票
# docs/archive/portability/TASK_KSTRING_BENCH.md)。実物の
# userland/tests/kstr_bench.c を 1 行も写さず #include し、KernelAPI
# (get_tick / sys_write / sys_yield) と測られる 13 本だけを贋物にして回す。
# 見るのは数字ではなく**数字の作り方**: 出力の固定書式、1 ケース 1MB 以上 →
# 30 ティック未満なら倍 (倍は 5 回まで = 時計が止まっても終わる)、食い違いを
# 注入したら MISMATCH が出てその関数の計測が飛ぶこと、13 本が 4 者 (.asm の
# global / 表 / 改名表 / 贋物) で一致すること、[V2] の登録。
# 最後に**実物の .asm と .c を同居させた版**も回して 13 本の一致を見る (受入 K1)。
# --mutate は否定側 (倍にしない版 / 欄を入れ替えた版 / 飛ばさない版 など)。
# 記録は tools/tests/kstr_bench_tdd.md。
check-kstr-bench-host:
	python3 -B tools/tests/test_kstr_bench.py --target $(MUT)

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

# 文書のリンク切れ検査 (lychee の薄い包み)。相対パスの実在と見出しアンカーの
# 実在を見る。900 リンクで 0.03 秒なので `check` の列に入れてある。
# lychee (cargo install lychee) が無い環境では SKIP して終了コード 0。
check-docs-links:
	@python3 tools/check_docs_links.py

# 孤児文書の検出 — docs/INDEX.md から辿れない docs/*.md と、どの票からも
# 参照されていない tools/tests/*_tdd.md。lychee の守備範囲外なので自前。
#
# **`check` の列にはまだ入れていない。** 2026-09-15 の棚卸し時点で 31 本 +
# 20 本が未参照で、これは検査の不備ではなく索引の取りこぼし (票を書いて
# INDEX.md に載せ忘れたもの) の実数。今これを門にすると、通すために
# `docs/.orphans-allow` へ全部書き写すことになり、例外表が「黙らせる表」に
# 化けて二度と減らない。**PM が文書整理の段階 F で索引を直し (載せるか
# archive へ移すか)、0 になった時点で `check` の列へ移すこと。**
check-docs-orphans:
	@python3 tools/check_docs_orphans.py

# 票の状態行の語彙 (docs/tasks/**/*.md の冒頭の `状態:`)。語彙は docs/POLICY_DEV.md §8 の
# 表から読む (道具は語を持たない)。2026-09-29 の v2.1 の棚卸しで語彙の外の状態行が 12 本、
# 実態と食い違う状態行が 27 本あったので足した。docs/archive/ は見ない (本文を書き換えない規則)。
check-docs-status:
	@python3 tools/check_docs_status.py

# 上の検査器のホスト試験。小さな木を作って判定を固定し、実物の木が通ることも見る。
# --mutate は検査器の写しに当てるので並列 (check-par) で回せる。記録は tools/tests/docs_status_tdd.md。
check-docs-status-host:
	python3 -B tools/tests/test_docs_status.py $(MUT)

# 試験一覧 docs/TESTS.md の鮮度検査 (文書整理 段階 E)。表は build/*.mk と試験
# スクリプトから tools/gen_tests_inventory.py が生成するので、ターゲットを足した
# のに一覧が古いままという状態を止める。check_kapi_version.py と同じ「生成物と
# 正典の照合」の作法で、ずれたら --write を促して落ちる。手書きの節は
# docs/TESTS.md の `<!-- manual:… -->` 区間だけで、生成器はそこを読み戻して保つ。
check-tests-inventory:
	@python3 tools/gen_tests_inventory.py --check

# CD のパッケージが配備マニフェストと一致しているか (tools/mkpkg.py --plan、
# 構成は build/packages.yaml)。配備の各ファイルがちょうど 1 つのパッケージに
# 入っている (または理由つきで除外されている) こと、userland/tests/ 由来が
# test タグであること、128 項目を超えたら分割されること、実物の mkpkg で作った
# PKG と ISO の中の PKG を読み戻して配備の全ファイルが同じバイト列で揃うこと、
# cdinst.c のベース名が構成と一致することを見る。成果物を読むので make all の後。
check-packages-host:
	python3 -B tools/tests/test_packages.py

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
#                       対応表は tools/check_map.yaml、選び方は tools/check_select.py
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

# 対応表 tools/check_map.yaml の検査 (列との過不足・古い glob・入力の漏れ)。
check-map:
	@python3 tools/check_select.py --lint

# check-changed の選び方の試験 (代行レビュー P2-1〜P2-3 の筋書き: .inc の取り込み、
# 裸の文書名、docs だけの変更で文書の検査を常に回す、走査型 glob の保険、
# feat/gui の上でコミットした後の基点。Makefile / build/*.mk の「新しい試験を足す形」
# の型の一致と、独立レビューの反例が全部に倒れること)。--mutate は check_select.py の
# 写しに当てるので並列 (check-par) で回せる。
check-check-select-host:
	python3 -B tools/tests/test_check_select.py $(MUT)

# キー注入 (票 docs/archive/tools/TASK_KEY_INJECT.md)。**np21w-src の
# aidebug_keys.cpp を実物のまま g++ にリンクして**変換表を照合する
# (エミュレータのソースはホストで試験できる)。np21w-src が無ければ SKIP。
# 併せて tools/gui_gate.py の逃がし記法と、**既定で既存の送信バイト列が
# 変わらないこと** (受入 K6) を送信の差し替えで見る。
check-key-inject-host:
	python3 -B tools/tests/test_key_inject.py

# tools/gui_gate.py の「GUI に入れたか」(R2 の予備調査 2026-09-29、POLICY_DEBUG §4-31)。
# 台本が rshell を閉じずに os32gui を打つと GUI に入らないまま RESULT: OK になっていた。
# 偽のゲスト (rshell の段数・GUI・tvram・/api/status) の上で、ESC → tvram の
# `[Remote shell closed]` の確認と、scrn_ymax == --h かつ grph_disp == 1 の照合を固定する。
# NP21/W は要らない。--mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/gui_gate_tdd.md。
check-gui-gate-host:
	python3 -B tools/tests/test_gui_gate.py $(MUT)

CHECK_PAR_TARGETS := check-shlib-high-host check-bootinfo-host check-hdd-stage1-host \
    check-hdd-stage2-host check-vmkernel-lz4-host check-vk32-crc-host \
    check-build-id-host check-kbd-status-host check-kbd-dlog-host \
    check-pcm-cs4231-host check-kprintf-attr-host check-key-inject-host check-gui-gate-host \
    check-kapi-version check-kapi-out check-docs-links check-docs-orphans \
    check-docs-status check-docs-status-host \
    check-tests-inventory check-manifests check-packages-host \
    check-constraints check-p2v check-c-dialect check-c-dialect-host \
    check-privileged check-arch-asm check-le-access \
    check-ne2000-ring check-shlib check-gui-proto check-term-model \
    check-term-render check-t5a-host check-memory-host check-memmap-host \
    check-memmap check-boot-splash-host check-tools-host check-np21w-ctl-host check-np21w-ini-live-host \
    check-gshell-host check-db-owned-host check-vfs-fd-sqlite-host \
    check-fdc-seek-host check-serial-vfast-host check-serial-portc-host \
    check-cpu-calibrate-host check-pit-clock-host check-dma8237-host \
    check-dma-pool-host check-pci-bind-host check-rshell-serial-host \
    check-serialfs-host check-vfs-mount-dev-host check-sqlite-groups-host \
    check-con-sink-host check-bootlog-host check-kbd-inject-host \
    check-launch-host check-ring3-str-host check-ring3-guard-host check-sh-launch-host \
    check-sh-shell-host check-sh-truncation-host check-multiapp-model-host \
    check-settings-protect-host check-hsync-h1-host check-hostdrv-list-host \
    check-fs-kind-host check-vfs-kind-host check-b8-open-host \
    check-ext2-empty-name-host check-vfs-fd-path-host check-db-v50-host \
    check-db-errstr-host check-cfg-host check-gui-host \
    check-install-recover-host check-install-fresh-host check-host-agent \
    check-net-link-host check-host-lib-host check-lan-bridge-host \
    check-pci-decode-host check-irq-math-host check-time-math-host \
    check-v86-gcap-host \
    check-fdc-track-host check-cd-read-host check-map \
    check-check-select-host \
    check-kapi-layout-host check-edit-doc-host \
    check-fstat-redir-host check-kstring-c-host check-kstr-bench-host \
    check-sh-status-host check-hsync-h3-host check-hsync-h2-host \
    check-h4-manifest-host check-vfs-excl-host check-fs-kind-callers-host \
    check-cat-linenum-host check-result-conv-host check-guest-host \
    check-fd-redirect-d0a-host \
    check-caller-access-host \
    check-access-walk-host \
    check-caller-copy-host \
    check-db-caller-host \
    check-surface-query-host \
    check-nano-inputs-host \
    check-surface-lease-host \
    check-cirrus-win-host check-pegc-mode-host
check-par: $(CHECK_PAR_TARGETS)

# エディタ GUI 版の本文と libos32gui の桁・折り返し (票 TASK_EDIT_GUI 受入 E8 / E10)。
# 実物の userland/rust/edit_gui/src/doc.rs と
# userland/rust/libos32gui/src/textcore.rs を `#[path]` でそのまま取り込み、
# 保存の経路だけ KAPI を差し替えて回す (ゲストもエミュレータも要らない)。
# 見るのは 3 つ: (a) 桁で折り返すときに UTF-8 の途中で切らない (§4-27)、
# (b) 「見える範囲」が行を飛ばさない / 重ねない、(c) 書けなかったのに成功と
# 答えない。加えて **WK_TEXTBOX の振る舞いが変わっていない** ことを、
# textbox が通っている共通の下請け (textcore) の変異が RED になることで見る
# (受入 E10。textbox が下請けを通っていること自体は静的に突き合わせる)。
# --mutate は否定側。記録は tools/tests/edit_gui_tdd.md。
check-edit-doc-host:
	python3 -B tools/tests/test_edit_doc.py $(MUT)

clean-sdk:
	rm -rf $(SDK_OUT) $(SDK_DIST_DIR)

.PHONY: check-c-dialect check-c-dialect-host check-docs-status check-docs-status-host check-gui-gate-host check-v86-gcap-host check-fast check-changed check-map check-check-select-host check-par check-packages-host check-kapi-layout-host check-bootinfo-host check-hdd-stage1-host check-hdd-stage2-host check-vmkernel-lz4-host check-vk32-crc-host check-build-id-host check-kbd-status-host check-kbd-dlog-host check-pcm-cs4231-host check-kapi-out check-dma8237-host check-dma-pool-host check-pci-bind-host check-kprintf-attr-host check-edit-doc-host check-memmap check-memmap-host sdk sdk-dist clean-sdk check-fstat-redir-host check-vfs-excl-host check-hsync-h2-host check-h4-manifest-host check-kapi-version check-manifests check-constraints check-privileged check-arch-asm check-le-access check-gui-proto check-term-model check-term-render check-t5a-host check-memory-host check-memmap-host check-memmap check-boot-splash-host check-tools-host check-np21w-ctl-host check-np21w-ini-live-host check-gshell-host check-db-owned-host check-vfs-fd-sqlite-host check-fdc-seek-host check-serial-vfast-host check-serial-portc-host check-cpu-calibrate-host check-pit-clock-host check-dma8237-host check-dma-pool-host check-pci-bind-host check-rshell-serial-host check-serialfs-host check-vfs-mount-dev-host check-sqlite-groups-host check-con-sink-host check-bootlog-host check-kbd-inject-host check-launch-host check-ring3-str-host check-ring3-guard-host check-sh-launch-host check-sh-shell-host check-sh-truncation-host check-sh-status-host check-multiapp-model-host check-settings-protect-host check-hsync-h1-host check-hsync-h3-host check-hostdrv-list-host check-fs-kind-host check-fs-kind-callers-host check-cat-linenum-host check-vfs-kind-host check-b8-open-host check-ext2-empty-name-host check-vfs-fd-path-host check-db-v50-host check-db-errstr-host check-cfg-host check-gui-host check-install-recover-host check-install-fresh-host check-host-agent check-net-link-host check-host-lib-host check-kstring-c-host check-kstr-bench-host check-result-conv-host check-guest-host check-guest check-arm-compile check-docs-links check-tests-inventory check-docs-orphans check check-lan-bridge-host check-pci-decode-host check-irq-math-host check-time-math-host check-fdc-track-host check-cd-read-host

.PHONY: check-p2v

check-shlib-high-host:
	@python3 -B tools/tests/test_shlib_high.py $(MUT)

.PHONY: check-shlib-high-host

# T2d d0a: known failure is explicit XFAIL until d0b fixes registered-AS copy.
check-fd-redirect-d0a-host:
	python3 -B tools/tests/test_fd_redirect_d0a.py $(MUT)

.PHONY: check-fd-redirect-d0a-host

# T2d d1: shared identity and actual syscall entry / normal return.
check-caller-access-host:
	python3 -B tools/tests/test_caller_access.py $(MUT)

.PHONY: check-caller-access-host

# T2d d3: managed tables/PFN with real paging and allocator.
# Run every explicit ILP32 control; mutate only the first runner. No fallback.
HOST32_RUNNERS ?= native qemu
export HOST32_RUNNERS
check-access-walk-host:
	python3 -B tools/tests/test_host32.py
	@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/test_access_walk.py --runner $$runner; done
	$(if $(MUT),python3 -B tools/tests/test_access_walk.py --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)

.PHONY: check-access-walk-host

# T2d d4: bounded copies through the real managed walk.
check-caller-copy-host:
	@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/test_caller_copy.py --runner $$runner; done
	$(if $(MUT),python3 -B tools/tests/test_caller_copy.py --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)

.PHONY: check-caller-copy-host

# T2d d5: actual DB wrappers and output guards through caller copy/walk.
check-db-caller-host:
	@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/test_db_caller.py --runner $$runner; done
	$(if $(MUT),python3 -B tools/tests/test_db_caller.py --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)

.PHONY: check-db-caller-host

# T2e e1: dormant query/authorization through real B1 and managed paging.
check-surface-query-host:
	@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/test_surface_query.py --runner $$runner; done
	$(if $(MUT),python3 -B tools/tests/test_surface_query.py --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)

.PHONY: check-surface-query-host

# T2f f1a: actual nano archives, providers and build provenance.
check-nano-inputs-host:
	python3 -B tools/tests/test_nano_inputs.py $(MUT)

# T2e e2: single-surface USER lease and transactional B1 copyout rollback.
check-surface-lease-host:
	@set -e; test -n "$(HOST32_RUNNERS)"; for runner in $(HOST32_RUNNERS); do echo "HOST32_RUNNERS=$$runner"; python3 -B tools/tests/test_surface_lease.py --runner $$runner; done
	$(if $(MUT),python3 -B tools/tests/test_surface_lease.py --runner $(firstword $(HOST32_RUNNERS)) $(MUT),@:)

.PHONY: check-surface-lease-host
