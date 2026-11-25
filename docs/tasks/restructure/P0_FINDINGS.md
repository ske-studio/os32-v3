# P0 の棚卸しの所見 (2026-11-25)

> 上位: [TASK_RESTRUCTURE.md](TASK_RESTRUCTURE.md)・[RULES.md](RULES.md) (入口と、どの段も先に読む決まり)

**判定 (票を書いたセッションのレビュー 2026-11-25)**: P0-2・P0-4 は使える。P0-1・P0-6・P0-7 は足りない (持ち主空 366・HAL の役割の未確定 295・模型とコンパイルだけの未確定 813/811 — 読んで埋める仕事は P0-1 の残りと P1-3・P5-2 へ)。P0-3 は一部 (L3 の 585 本は covers から機械で付けた候補、P1-4 で読んで決める。変異の数は 261 本だけ)。P0-5 は表は使える (結論は下の P0-5 の節)。P0-8 は一部 (未確定 100、ノートと pc98emu への質問 40 行 remote.tsv は 2026-11-25 に送付・回答済み、要約は末尾の「P1 への入力」)。

基点は全段とも `4146cf146`。P0-3・P0-4 の CI 実測は `3b1cecee3` の保存ログと P0-4 の push 履歴を使った。書き手も読み手も astra (DECISIONS の U8)。前回の読み手の所見と残る漏れは各節に記す。

- 追跡 6,513 件から道具・試験 17 ファイルと検査登録ファイル 8 件を除いた母数は 6,488 件。
- 検査登録 822 本から同じ 8 登録を除いた母数は 814 本。除外パス一覧は生表の親の `excluded`。
- 生表は `/home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-<n>/`。CI の入力は `~/os32-tmp/run/restructure/p0-4/ci/` と `~/os32-tmp/run/restructure/ci-logs/`。

## P0-1

- 除外後の追跡 6,488 件と `owners.tsv` の 6,488 行はパス・順序とも一致。ヘッダなし。列はパス、種類、所属の根拠、入る像、Make 変数、check_layers 分類、集合調査結果。
- 明示分類・集合・像の根拠が空の行は `owner-empty.tsv` に 366 件。像に入らない開発・検査用 2,555 件、文書 1,274 件、公開インターフェース 1 件に分類規則を付けた。既定の common は分類列に残した。
- 像に入らない公開インターフェースは `sdk/kapi.json` の 1 件。基点で追跡する `sdk/include/` の 19 ヘッダはすべて像の入力でもあり、仕様 §5 の所有の表では公開の所属と依存先の像を併記する。
- `owner-empty-reasons.tsv` は置き場・拡張子・理由・件数の 51 行。内訳は調査対象外のソース 102 件、調査済みで集合も像もないソース 7 件、像への依存を確認できなかったヘッダ・inc 29 件、その他の明示分類・ビルド根拠なし 228 件。
- ソース 2,282 件。Make 集合あり 1,151 件、集合を確認できなかった `no-set.tsv` は 216 件（像だけ判明したものを含む）、調査対象外の `out-of-scope.tsv` は 915 件。`multi-feature.tsv` は 80 件。
- PC-98、PowerMac 各構成、独立 PowerPC bootstrap の Make 規則から採取。MMU_C の 4 ソースは PPCMMU.ELF、pkg.c は PPC_INST_EXTRA と cdinst.elf に所属。ext2_dir.c の機能は IDE のみ。
- Cargo metadata（offline）の crate 根と生成済み Cargo 依存表から .rs を照合。hello_gfx/src/lib.rs → hello_gfx.elf、直接生成規則で boot_fat.asm → boot_fat144.bin、コンパイラ依存で exec_abort.inc → PC-98 kernel.elf を確認。
- 生表は `/home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-1/`。像はビルド入力としての所属。最終媒体への収録、依存ファイルがない構成の条件付き include は未確認。
- 残る漏れ (読み手 2026-11-25): リンカスクリプトの像への所属 (boot/loader.ld は通常・デバッグの両 ELF のリンク入力だが像の欄が空) と、ELF から変換する BIN へのソースの所属 (boot/boot_main.c → loader_hdd.bin、build/boot.mk) は表に反映されない。

## P0-2

- 生表のデータ行数（見出し除く） build_refs.tsv 3431、include_order.tsv 3132、link_differences.tsv 680、link_evidence.tsv 36577、link_order.tsv 196、link_sources.tsv 403、read_mk.tsv 200。
- 構成ごとの .mk 読込 pc98 56、powermac 27、kboot-core 18、kboot-link 18、kboot-shell 27、kboot-sh 27、kboot-link-sh 27。異なる 71 本、未収録 0 件。
- geforce.elf の GEFORCE_OBJS 由来の前提は初期定義16 件、追記123 件。先頭 geforce_body.o の候補はこの変数では初期定義だけ。
- common.mk、programs.mk、ppc-user.mk のリンク出所と ppc-user.mk のパス組み立て分類を確認。
- 文面のリンク分類だけにある行 453、データベースからだけ拾った行 227。
- 残る漏れ：同一ファイル内の追記を最終値の末尾と照合する。複雑な関数を含む追記や別ファイルにまたがる定義は元の行を確定できない。同じ語を持つ別変数も候補に含まれる。
- 生表の所在 /home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-2/。
- 残る漏れ (読み手 2026-11-25): 条件分岐で実行されなかった再代入が追記の前にあると、初期値の出所をその再代入の行として記録する場合がある (tools/restructure/build_refs.py)。

## P0-3

- 除外後の登録 814 本 = `checks.tsv` 814 行。名前の過不足・重複 0。
- covers 由来の分類・行き先がそろうのは 807 行。欠ける 7 行は `no_candidate.tsv`。分類別は {'L0': 15, 'L1': 2, 'L2': 143, 'L3': 585, 'L4': 61, 'M': 4, '空欄': 4}、L5 は 0。
- env から L4 を示唆する 58 行は別列に掲載。covers でも L4 が 43 行、env のみが L4 を示すのは 15 行。
- CI 所要は保存済み p0-4/ci/times の成功行から環境を合わせた中央値で 814 行、空欄 0。
- 変異は `3b1cecee3` の check 45568507923・build 45568508091 が対象。同じ run/shard の成功記録で対象を絞り、過去の個別ログの検査固有の出力と照合して、今回の CI の個別 RED 行を数えた。表にジョブログ名と行番号を記録。
- 変異数が取れたのは 261 行。未確定 553 行の内訳は、過去の個別ログに個別 RED が見つからないもの 51、個別 RED はあるが今回のログで帰属・完全性を確認できないもの 401、対応する過去の個別ログなし 43、gate が成功ログ本文を省略したもの 58。
- e82557-eeprom-host は 21、gshell-host は 185（183 + 2）、allocator-bench-host は 5、abi-boundary は 2、edit-doc-host は総括を除いて 11。ソース推計との不一致 19 行を `source_disagreements.tsv` に掲載。回数の単位は RED 報告行で、同じ変異を別 runner で報告する場合も各行を数える。
- 生表・基点の登録 JSONは `/home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-3/`。

## P0-4

### gate.md §5-1 追記案 — 2026-11-25 の直近 20 push

- 02:59〜08:57 UTC の push を ref と SHA の組で識別した。タグを含む。workflow は 32 run、ジョブ記録は 31 run・536 件。実行番号・SHA・時刻は `pushes.tsv` と `runs.tsv`。
- push は成功 15・失敗 3・取消 1・進行中 1。成否確定分の成功率は 15/18（83.3%）。run は成功 27・失敗 3・取消 1・進行中 1。
- ジョブは成功 508・失敗 7・取消 1・skipped 20。成否確定分の成功率は 508/515（98.6%）。ジョブ名別の分母と率は `job_rates.tsv`。
- `jobs.tsv` の実行ジョブ 516 件で、作成→開始の待ちは 1〜1,671 秒（中央値 11 秒）、開始→終了は 6〜1,877 秒（中央値 14 秒）。skipped の時刻差は空欄。
- 最長の待ちは run 45568786180。push 全体（最初の run 作成→最後のジョブ終了）の最長は 2,999 秒、main の 45572557040・45572557120。
- 失敗した検査名をタイマーから 9 件特定（45564965103 が 1 件、45548967128 が 8 件、名前と終了値は `failed_checks.tsv`）。45579912559 は static-check の文書検査段まで特定。
- 成功した直近の統合枝 5 push・10 run のタイマー 130 本を既存の time_inventory.py で集計。各 run 内のシャードの偏りは QEMU 1.44〜2.23（4/5 回が 1.5 超）、x86 1.29〜2.26（3/5 回）。`shard_balance.tsv` に run ID 別の値。
- 重い順の先頭は memmap-host（中央値 881 秒）。重みのずれ等で既存道具が挙げた検査は 99 本。上位 15 と判定は `time_inventory.md`。G10 の再計測の基準にする。
- ログの基準は wt/int-1125c（3b1cecee3）の全件 CI 1 回。check 45568507923・build 45568508091 の 32 ジョブ中、本文 30 件で 139,581,115 バイト。skipped 2 件の BlobNotFound 応答は除外した。
- gate.py と同じ行頭 SKIP は 123 行。本文の検査名を登録名と照合して 18 行を帰属（ppc-sc・ppc-sd・ppc-sh-output は各 4 行）。残る 105 行は帰属未確定。gate の検査名付き SKIP 集計は 0 行。
- 変異の結果を示す書式に合う RED の行は 9,042 行 (行頭 RED 4,967、変異を示す接頭辞 3,771、行頭 runtime 298、名前付き runtime RED with reach marker 6)。**これは変異の件数の下限ではない** — 同じ変異の個別の結果と集計の行 (例 `RED ISO rd name (exit 3)` と `RED 1/1`、ci-logs/45568508091-checks__0_.log) が両方入り、二重に数える行がある。検査名に帰属できた結果行は 0。ほかに判定できない RED の行が 1,334 行あり、static-check が個別の結果を `RED (落ちた)` で終える 8 行を含む (ci-logs/45568507923-static-check.log) — 読み手 (2026-11-25) の所見を PM が書き換え。変異の総数はログからは確定できない (次の行)。
- CI のログに検査ごとの構造化された結果（検査名・SKIP・変異の数）が無いので、総数はログからは確定できない。P6 で gate の結果に持たせる候補。
- ジョブ別は log_jobs.tsv、検査名付きの行は log_checks.tsv、ジョブ・書式別の内訳は red_formats.tsv。残りの run は「1 回分で代表」。別件の未取得は 45579912559 の失敗段 stdout と 45580917618 の完了時 jobs JSON。
- 生表の所在は `/home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-4/`。

## P0-5

- **表から読める結論** (票を書いたセッションのレビュー 2026-11-25): (A) `include/` を移すと kdebug の生成物が変わる — kdebug.elf と kselftest.o に文字列 `include/memmap.h` が入る (artifact_paths.tsv の runtime string の行)。カーネル側の置き場のパスが出荷物に入るのは抜き取った範囲でこの 1 件。P3 の W9 はバイト一致にならず `Sep-Changes:` の宣言つきの波になる。(B) `userland/libos32gui.elf` に作業木の絶対パス (`…/rs-p0-rerun/userland/lib/font/src/raster.rs`) と rustup の置き場が入る — 置き場の移動と関係なく作業木が違えば生成物が変わる (POLICY_CI G11)。(C) `move_files.py` が置換できない所は unresolved.tsv 7,517 行。手で直すと確定しているのは split_paths 10 と unresolved_paths 312 (部品の .mk の .o のパスなど)。directory literal 2,923 と constructed expression 1,450 は使われ方が未確定で、P2-6 の予行で分かる。
- 生表は /home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-5/。行数は見出しを除く。
- 仮表 map.json は 1,802 ファイル。dry-run.jsonl は 4,648 実行、置換差分あり 2,731。dry-run-full.json は tools/tests/host_arch/arch_abi.h の全対応表による照合。
- refs.tsv は 82,351 観測、うち根拠が未確定 22,883。path_refs.tsv は種類×根拠 23 行。現行票 P3.md の init/kernel_start.c は refs と dry-run の両方で確認。
- 種類別の出現数：__FILE__ source 21、allow 76、baseline 185、constructed path 3,243、covers 21,343、document 15,998、mutation context 221、mutation operand 9,248、skill 97、source/build 10,101、tools 21,774、workflow 44。
- effective_covers.tsv は814 登録のうち covers が空の build-no-deploy を除く 813 検査・21,102 行。
- unresolved.tsv は 7,517 行。constructed expression unchanged; evaluation 未確定 1,450、directory literal; use 未確定 2,923、excluded holder 463、glob outside literal map 1,838、include directory not supplemented by dry-run 521、split_paths 10、unresolved_paths 312。
- products.tsv は 2,838 成果物。make のログ・.d・配布表・無視対象の生成物を走査。build-start は PC-98 の build.log 作成時刻で、以後に更新された .o・.elf・.a も対象。各行の product_evidence に選定根拠を記録。
- 前回の PowerMac 像に加え、今回は ppc-ci-image の生成物も走査したため、成果物とシンボルの観測が増えた。
- artifact_paths.tsv は 24,623 行。DWARF line CU 3,572、DWARF line file 2,856、DW_AT_comp_dir 171、DW_AT_name 3,572、STT_FILE 9,662、path bytes 4,758、runtime string .rodata 31、runtime string .rodata.str1.4 1。gfx/v2/format.o の STT_FILE format.c は build.log と gfx/v2/format.d から gfx/v2/format.c に帰属。
- 生成物にスラッシュ付き表記が入る TU は 136、うち表記先も照合できた TU は 135。basename 等を含めると 1,148 TU。生成元が未確定の観測は 7,139 行。PC-98 と PowerMac の同一ソースは1回数えた。
- 未確認：動的な式・変異の実行時の参照先、__FILE__ の展開由来、LLVM bitcode を含む書庫 12 件の非 ELF メンバー、圧縮内容の展開後だけに見える表記。全 CI・ゲスト受入は未実行。
- 残る漏れ (読み手 2026-11-25): D88 の形の生成物は探索の対象から漏れる — images/os32_boot.d88 (build/image.mk) に fpdummy.c のパスが残るが products.tsv・artifact_paths.tsv に無く、集計の外。

## P0-6

- 表の行数は見出しを除く。全 1062 行に file・判定・根拠あり。
- hal.tsv は 366 行、360 ファイル。役割の判定は 未確定 295、実コード 71。
- 各分野の内訳は 契約／共通処理／振り分け／バックエンド／未確定（行）。
- 音　1／4／3／2／29
- 入力　2／2／1／3／39
- 描画　2／3／2／19／142
- ブロック　2／1／1／6／32
- LAN　1／1／2／3／42
- 時刻　2／2／1／5／11
- HAL の移設判断は 未確定 317、移動のみ候補 33、P8候補 16。操作表を型で走査し、fs/ext2/ext2_medium_ide.c を収録。
- drivers/ata/atapi.c は atapi_read_sectors の本体で atapi_read10 を呼ぶ。drivers/video/geforce/edid.c は gf4_edid_modes の本体で EDID の checksum を調べる。
- p8.tsv は 452 呼出し式、63 ファイル、177 関数。P8候補 304、未確定 148。
- fs/fat/fatfs_vfs.c → drivers/block/fat_diskio.c、kernel/sys.c の geforce_foreground_poll など、直接の呼び出しと定義の所在を確認、変え方（呼び出し口か内部の振り分けか）は未判断。
- 追跡 C/H/INC の関数定義と build/out/obj の nm（外部 T/W）を照合。共通側にも定義がある名前、同名のマクロやヘッダ等の局所定義を把握した名前は未確定。
- geforce.tsv は 244 ファイル。個別の役割は 未確定 199、実コード 45。群の役割は配置からの推測。
- HAL の移設判断・P8 の変更判断は候補。間接呼出し、マクロ展開、全構成のリンク先、移設後の生成物一致は未確認。
- include/ の共通ヘッダで宣言され、複数機種に定義がある契約の呼出しを63件除外。platform_clock_read_us は0件、geforce_foreground_poll は候補2件。
- 残る漏れ：platform_irq_topology は include/platform_irq.h に宣言があるが、定義を確認できたのは platform/pc98/irq_policy.c の1機種のみ。4呼出しが候補に残る。
- 表は /home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-6/。
- 残る漏れ (読み手 2026-11-25): 共通の契約 platform_host_owner_exit は kernel/dev/device_host.c と platform/powermac/host_unavailable.c に定義があり、片方が platform/ の外にあるため p8.tsv に未確定として残る。

## P0-7

登録 814 本 × 5 条件 = 4070 行。条件は表頭に 5 件。

| 条件 | 候補（登録） | 条件外 | 未確定 |
|---|---:|---:|---:|
| 段名 | 58 | 756 | 0 |
| 模型 | 1 | 0 | 813 |
| コンパイルのみ | 3 | 0 | 811 |
| 同一宣言 | 7 | 638 | 169 |
| 変異50超 | 8 | 255 | 551 |

同一宣言は 5 組。空の宣言は未確定。
R1 型は multiapp-model-host の模型部分。同行の実物試験とは別に模型を単独リンクして実行する。
コンパイルのみは直接追跡できた全試験のプロセス呼出しで判定。abi-boundary は生成物の終了値を調べるため候補から除外。
変異は CI run 45568507923・45568508091 の個別 RED 行で 261 登録を計測。50 超は 6 登録（gshell-host 185）。ほかに静的な表からの候補が 2 登録あり、実行数は未確定。
動的な委譲・間接的なソース入力は静的解析の範囲外。候補の採否は未確認。
生表: /home/hight/os32-tmp/run/restructure/p0-rerun/rerun-4146cf146/p0-7/quality_candidates.tsv
- 残る漏れ (読み手 2026-11-25): 段名の条件は L＋番号を拾わず、dep-dir-a-l01-host など同じ系列の 9 登録が条件外になる (tools/restructure/quality_candidates.py)。
- 前回の 815 登録には restructure-p0-7 自身が入っていた。今回は P0-3 と同じ登録を調査した。

## P0-8

相手・パス・用途別に 638 件。

| 相手 | 変わる | 変わらない | 未確定 |
| --- | ---: | ---: | ---: |
| NP21/W 予行 | 0 | 1 | 3 |
| PM | 0 | 0 | 1 |
| apps | 0 | 12 | 0 |
| ci_fetch | 0 | 15 | 0 |
| deploy-set | 1 | 470 | 72 |
| game | 0 | 11 | 0 |
| np21w-src | 0 | 1 | 10 |
| pc98emu | 6 | 20 | 5 |
| 実機ノート | 0 | 1 | 8 |
| 実行機未確定 | 0 | 0 | 1 |
| 合計 | 7 | 531 | 100 |

pc98emu の MCP 起動設定と import をたどり、gui.py が読む exec/ のヘッダ1件、include/ の3件、kernel/module/ の2件を通知対象にした。userland/gshell の入力2件は維持。
実機更新手順の NP21/W 予行で使う nhd_deploy.py・hostdrv_deploy.py・np21w_ctl.py は予行の相手に計上。実行機を特定できない記述は独立行とした。
deploy-set の arch/x86/ 配備元1件は移動対象。apps・game の固定版で確認できた明示入力は staged SDK。np21w-src の旧 checkout 表記と実運用の対応は未確認。
notify.tsv（17行）・waves.tsv（120行）で W1〜W11 と T 各波の旧パスを照合。個別の波番号は P1 待ち。
remote.tsv（40行）は実機ノート・pc98emu への質問。実機とリモート枠の現物、GitHub の配布物、動的に組む参照は未確認。H-7 の外部ビルドは未実行。
- 残る漏れ (読み手 2026-11-25): 分割して組み立てる固定のパスと import 先のファイル自体に未収録の例がある (NP21/W の予行の SDK ヘッダ sdk/include/os32/os32_kapi_shared.h — tools/nhd_deploy.py、実機ノートの /opt/os32/tools/host_linux.py — tools/host_agent.py)。

## P1 への入力 (P0 の取り直しのレビュー、票を書いたセッション 2026-11-25)

- P0 の取り直し (基点 4146cf146) は受け取り可。P0 の回し直しは要らない。
- 持ち主が空 366 のうち約 100 は規則の漏れ: 持ち主が空の .rs は 93 本 — userland/lib/font/vendor/ttf-parser の 64 本 (Cargo.toml の dev-dependencies、製品に入らない。ほかに同じ vendor の .rs でない 5 本)、ホストの試験の置き場 host/・host_tests/ の 26 本 (規則が「tests」という名前のディレクトリしか拾わない。同じ置き場に .rs でない 6 本、試験の本体と支援 mocks.rs の類が混ざる)、残り 3 本 (userland/gshell/src の fixture.rs・typical_ops.rs、libos32term_render の rework_tests.rs)。前の版の「32 本 + 64 本」は数え違い (sol の指摘 2026-11-25)。
- 「公開インターフェースは 1 件」は狭い: 規則が sdk/include/ と sdk/kapi.json だけで、sdk/ の 29 本が未分類 (sdk/link の .ld 6、mkos32x.py など .py 15、Rust のターゲットの定義 3 ほか)。
- 仕様 §5 の 6 種類に当たらない物: build/ の .mk 74 とリンカスクリプト 16、assets/ 22、sample/ 10、文書 1,274。
- 残骸の候補: kernel/v86 の .h 5 本、include/ の diag_stack.h・snd_writer_probe.h・type_contract.h・vk32.h はどの像の入力でもない。
- P0-6・P0-7 の未確定は前と同じ (295・813・811)。HAL の表で役割または移設の判断が未確定の行は和で 326 (役割 295・移設 317)。
- **外の利用者への質問の回答** (P0-8 の remote.tsv の 40 問、ユーザー承認 2026-11-25 で送付、同日に回答): 実機ノートは /opt/os32 (os32-v3 全体の git の作業木) の tools/ を常駐のサービスが使い、作業木の外に tools/ の写しが 37 個 (~/os32-tmp/tools-<8 桁>/、版の記録なし) と G4 の常駐 (~/os32-tmp/ppc-rh/wt-rd で tools/host_agent_service.py を相対パスで起こす) がある — tools/ を動かす・分ける時は知らせる。pc98emu は tools/ (np21w_ctl・np21w_mcp/np21w_client・gui_gate・ci_fetch・accept/snd2・ppc の 3 本) を sys.path と git archive で使い、ソースを実行時に読む道具が 4 本 (tools/ppc/root_lz4_layout が include/memmap.h、root_lz4_layout_run が tools/gen_memmap.py・lib/kstring_c.c・arch/ppc/ofw_note.S、link_sh_run が userland/shell と kernel/console/con_sink.c、accept/snd2/verify_soft が lib/os32_opna/upstream) — これらは木の中の道具なので P3 の波で一緒に直す。tests/ へ移る時に pc98emu の側で直すのは tools/tests/make_pcm_wav.py と pcm_play_guest.py の呼び出し 2 か所。回答の全文は PM の機械の ~/os32-tmp/run/restructure/remote-answers-*.md。

