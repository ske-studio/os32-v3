# 試験一覧

手書きの読み方・判断はこの文書が正典。生成表の情報源は
`build/checks.d/*.mk` / `build/*.mk` と試験スクリプト。
`make tests-inventory` で `build/out/TESTS.md` と `build/out/HOST32.md` を生成する。
生成物はコミットしない。全生成物の見方は [INDEX.md](INDEX.md#生成文書の見方)。
`make check-tests-inventory` は登録列・recipe・試験スクリプト・入力対応表を検査し、
生成済み本文との一致は検査しない。

判断は `<!-- manual:… -->` の区間に手で書く。生成器はこのファイルを変更しない。
過去の調査は 2026-09-14 の快照 (アーカイブ: `docs/archive/TEST_INVENTORY_2026-09-14.md`)。

## 0. 読み方

<!-- manual:intro -->
### 表が言えること / 言えないこと

表は `build/*.mk` と試験スクリプトから**機械で読める事実だけ**を出す。次の 3 つは
生成できないので、この文書では列に無い。

- **件数** (表明の数)。数え方が種類ごとに違う — C は `check()` / `CHECK()` の呼び出し数、
  Rust は `#[test]` の数、Python は `def test*` または `CASES` の長さ。合算に意味が無い。
- **実行時間**。検査ごとの壁時計は `make check` が `build/out/check_times.tsv` に残す (2026-10-23 の全体検査で全本を計測、重い 4 本は 2026-10-24 の票 TASK_TEST_TIDY で 3,032→982 秒)。ソースから読める重さの根拠は、
  `check-cfg-host` / `check-db-errstr-host` / `check-install-recover-host` などは SQLite の amalgamation
  (`lib/sqlite3/sqlite3.c`、**267,757 行**) を**それぞれ独立に**毎回コンパイルする。
  `check-sqlite-groups-host` / `check-vfs-fd-sqlite-host` は2026-10-24 に削除 (票 TASK_TEST_TIDY の TT5)。
- **試験の種類**。5 つある — 静的整合性 / ホスト模型 / 実物ソースを `#include` /
  ゲスト観測 / 起動時自己試験。「実物ソースを `#include`」は、出荷するソースをそのまま
  ホストで走らせる形 (模型ではない)。表の「対象ソース」列が埋まっているものはだいたいこれ。

### 表の読み落としやすいところ

- **`make check` は単独では走らない**。`check-manifests` は `make all` の成果物を見る、
  `check-privileged` は `userland/**/*.o` が無いと exit 2
  ([`tools/check_privileged.py`](../tools/check_privileged.py))。
- **対象ソースが空欄でも試験が無いわけではない**。`tools/check_*.py` 系はツリー全体を
  走査するので特定のソースを持たない。Rust の 4 本 (`check-term-model` /
  `check-term-render` / `check-t5a-host` / `check-gui-host`) と `check-gshell-host` は
  `#[path]` 取り込みや `integration.py` の実行時走査なので、静的には拾えない。
- **`check-privileged` は構造上落ちない**。findings があっても `--strict` を付けなければ
  `return 0`。表の ○ は「`check` の列にある」であって「門である」ではない。
- **`tools/tests/paging_rebuild_host.c` の現行 2 モードは自動ゲートで走る**。
  [`memory-host.py`](../tools/checks/memory-host.py) は
  [`test_paging_bounds.py`](../tools/tests/test_paging_bounds.py) に `--rebuild nonmaster` /
  `--rebuild rollback` を渡す。旧 `sparse` / `attrs` / `final` は対象の legacy 分岐とともに撤去済み。
  workspace の PTE 検査は `memory_boot` の `metadata_pte` / `workspace_pte` が担う
  ([ハーネス冒頭](../tools/tests/paging_rebuild_host.c))。
- **`apps/` と `game/` はサブモジュール**で、切り出した worktree では空のことがある。
  それらの中のホスト試験はこの表に出ない (`make external` 側)。
`check-gserial-host` は [`tools/gserial.py`](../tools/gserial.py) の実物を偽ポートで走らせ、
乱数 id の終端照合、通知・付随行、GUI 入場、CUI 復帰の 3 段と EOT 後続の保持を検査する。
[`test_gserial_host.py`](../tools/tests/test_gserial_host.py) の `--mutate` は正常対照の後に
受信・照合の変異を回す。実機 UART・gshell の起動時間はこの試験では確認しない。
<!-- /manual:intro -->

## 4. `make check` の外にある試験

<!-- manual:outside -->
a5 の `check-host-agent` / `check-host-lib-host` / `check-net-link-host` /
`check-lan-bridge-host` は保守外 (2026-10-24 ユーザー決定、現物が手に入らない)。
ファイル・入力対応表を保持し、`make <名前>` でだけ実行する。対応表の `local_only: true` は
選択器と inventory の双方が通常の列から区別する。

以下は `check-*` ターゲットを持たない試験。自動ゲートではないが、契約を守っているのはこちら
という場面がある。

| 名前 | 種類 | 何を守っているか | いつ走る |
|---|---|---|---|
| [`kdebug/kselftest.c`](../kdebug/kselftest.c) | 起動時自己試験 (KDEBUG=1 のときだけ。OFF は 0/0) | カーネルが**実際に使う**プリミティブ (`kstring_asm.asm` / `kmalloc.c` / `kprintf`) の境界ケース。外部プログラムの `klibc_test` は newlib を測るので代替にならない。E08 の time・launch・dbv50・resume・irq・con_sink を統合し、人工状態はホストへ移管、con_sink の描画2判定は維持。[現行受入の正典](../tools/tests/guest_acceptance.yaml) の f13-mem-stat は共通下限217、IRQ3 IRR空なら223、fail=0 (統合後ゲスト未実測)。結果は `kselftest_pass` / `kselftest_fail` に残り `kernel.map` 経由で読む | 毎起動 (実機) |
| `link_selftest()` ([`drivers/net/ne2000/lgy98.c`](../drivers/net/ne2000/lgy98.c)) | 起動時自己試験 | LGY-98 リンク層の HELLO + PING/PONG。**`make kernel-lgy98-link` (LGY98_FLAG_LINKTEST) でビルドしたカーネルでのみ**走る。判定は `tools/net_l0_test.py` が `link_*` グローバルを `/api/mem` で読む | LINKTEST 版の毎起動 |
| [`tools/gui_gate.py`](../tools/gui_gate.py) | ゲスト観測 (判定は人) | GUI のゲート操作列 (`v11` / `v12g1` / `v12g4` / `shot` / `click`)。**自動照合は `wab_relay` / `scrn_ymax` / `fault_generation` の 3 数値だけ**、他は PM が目で見る | 手動 |
| `tools/emu_agent/tasks/regress.txt` | ゲスト観測 (ローカル AI) | 配備後の定型回帰 — kselftest カウンタ / `klibc_test` / `alloc_demo` / `ring3_fault` で落ちてもシェルが生きている / パイプライン 2 本 / screenshot。**モデルは観測を転記するだけで合否は人** | 手動 |
| `tools/emu_agent/tasks/v86_dos.txt` | ゲスト観測 (ローカル AI) | V86 の DOS 起動。`dos5hd.nhd` / master は 2026-09-10 に削除済みで、再作成は FORMAT から 30 分 | 手動 |

CI の静的ゲート (`.github/workflows/check.yml`、os32-v3 で 2026-09-30 に作り直し) が走らせるのは
生成表 (`build/out/TESTS.md`) の「CI ○」の `make <target>` (yml の行から生成) と、CI 専用の「生成物が `sdk/kapi.json` と
一致するか」(`gen_kapi.py` / `kapi_rust_gen.py` を回して `git diff --exit-code` で 5 ファイル)。
クロスコンパイラと rustc が無い状態で rc=0 になる検査だけを載せているので、末尾で `i386-elf-gcc` を
使うホスト試験と rustc が要るものは本体ビルドの `build.yml` の `make check-fast` が回す
([08_build.md §8-6](08_build.md))。**`check.yml` の成功を GUI やカーネルの動作確認とは扱わない**。
<!-- /manual:outside -->

## 5. 判断と過去の調査

<!-- manual:findings -->
ホスト試験と起動時自己試験は、同じ契約を扱っていても一律に重複とは扱わない。
ホスト側の境界ケースと、実機で毎起動踏む煙試験には別の役割がある (§4)。

| 今も残る項目 | 現行コードで確認した状態 |
|---|---|
| b2 | `build/checks.d/check-privileged.mk:5` は `--strict` を付けず、findings があっても失敗しない。ゲート化の扱いは未決。 |
| c3 / d1 | `tools/tests/test_hsync_protect.py:10` のとおり、`hsync` の inode 比較はコンパイル確認だけで、動作を検査しない。 |
| R1 | `tools/tests/multiapp_model_host.c:6` の純粋な模型が残り、実物ソースの状態遷移を試験していない。 |

2026-09-14 の分類・改善案 R1〜R12・未読記録の経緯: `docs/archive/TEST_INVENTORY_2026-09-14.md` §2〜§4。
今も残る項目は上の表だけ。たとえば R8 の再構築試験は
現在の 2 モードが自動実行される (§0)。現行の CI 被覆は §4 と生成表で確認する。
改善案の採否を再検討するときは、現在の recipe・試験本体と照合する。
延ばした試験の現行の判断は [DEFERRED_TESTS.md](tasks/DEFERRED_TESTS.md) に集約する。
<!-- /manual:findings -->

## 6. 試験記録への案内

<!-- manual:recommend -->
生成表の「記録」列は、スクリプトや Makefile が示す記録と語幹の一致から `*_tdd.md` を引く。
規則は [`gen_tests_inventory.py`](../tools/gen_tests_inventory.py) 冒頭を参照する。
列に現れない記録は削除せず、試験スクリプトの冒頭 40 行に
`記録: tools/tests/<x>_tdd.md` を足すか、ターゲット直前のコメントから案内する。

快照後の補足: 2026-09-15 の R12 確認では、語幹から辿れる記録は大部分解消した一方、
`gui_review_20260910_tdd.md` / `gui_review3_…` / `gui_review4_…` (対応ターゲットなし)、
`gshell_buttonup_tdd.md` / `k5b_gshell_tdd.md` / `s4_tdd.md` (integration.py 経由)、
`k6_ram_tdd.md` (別の記録が選ばれる)、`s5_tdd.md` / `t8_tdd.md` (参照が冒頭 40 行の外) は
案内の残件として記録された。`physmem_synthetic_source_tdd.md` は 2026-09-16 に
`physmem_tdd.md` へ改名して解消した。これは当時の追記であり、現在の未解消一覧ではない。
<!-- /manual:recommend -->

## 7. 調査の範囲

<!-- manual:unread -->
過去の未読範囲と未計測項目は 調査の快照 §4 (アーカイブ: `docs/archive/TEST_INVENTORY_2026-09-14.md`) にある。
生成表に情報が無いことと、試験が無いことは区別する (§0)。
<!-- /manual:unread -->
