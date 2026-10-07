# 試験一覧

手書きの読み方・判断はこの文書が正典。生成表の情報源は
`build/checks.d/*.mk` / `build/*.mk` と試験スクリプト。
`make tests-inventory` で `build/out/TESTS.md` と `build/out/HOST32.md` を生成する。
生成物はコミットしない。全生成物の見方は [INDEX.md](INDEX.md#生成文書の見方)。
`make check-tests-inventory` は登録列・recipe・試験スクリプト・入力対応表を検査し、
生成済み本文との一致は検査しない。

判断は `<!-- manual:… -->` の区間に手で書く。生成器はこのファイルを変更しない。
過去の調査は [2026-09-14 の快照](archive/TEST_INVENTORY_2026-09-14.md)。

## 0. 読み方

<!-- manual:intro -->
### 表が言えること / 言えないこと

表は `build/*.mk` と試験スクリプトから**機械で読める事実だけ**を出す。次の 3 つは
生成できないので、この文書では列に無い。

- **件数** (表明の数)。数え方が種類ごとに違う — C は `check()` / `CHECK()` の呼び出し数、
  Rust は `#[test]` の数、Python は `def test*` または `CASES` の長さ。合算に意味が無い。
- **実行時間**。**1 本も計測していない**。ソースから読める重さの根拠だけは分かっていて、
  `check-cfg-host` / `check-db-v50-host` / `check-install-recover-host` /
  `check-sqlite-groups-host` の 4 本は SQLite の amalgamation
  (`lib/sqlite3/sqlite3.c`、**267,757 行**) を**それぞれ独立に**毎回コンパイルする。
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
- **`tools/tests/paging_rebuild_host.c` (5 モード) はどの自動ゲートからも到達しない**。
  [`test_paging_bounds.py`](../tools/tests/test_paging_bounds.py) は `--rebuild` を
  付けたときだけこのハーネスを選ぶが、`check-memory-host` は引数なしで呼ぶ。
- **`apps/` と `game/` はサブモジュール**で、切り出した worktree では空のことがある。
  それらの中のホスト試験はこの表に出ない (`make external` 側)。
<!-- /manual:intro -->

## 4. `make check` の外にある試験

<!-- manual:outside -->
`check-*` ターゲットを持たない試験。自動ゲートではないが、契約を守っているのはこちら
という場面がある。

| 名前 | 種類 | 何を守っているか | いつ走る |
|---|---|---|---|
| [`kernel/kselftest.c`](../kernel/kselftest.c) | 起動時自己試験 | カーネルが**実際に使う**プリミティブ (`kstring_asm.asm` / `kmalloc.c` / `kprintf`) の境界ケース。外部プログラムの `klibc_test` は newlib を測るので代替にならない。f5a の appmem owner 往復 5 件を含む標準起動の期待は 288/0 (ゲスト未確認、台帳 F-6)。結果は `kselftest_pass` / `kselftest_fail` に残り `kernel.map` 経由で読む | 毎起動 (実機) |
| `link_selftest()` ([`drivers/lgy98.c`](../drivers/lgy98.c)) | 起動時自己試験 | LGY-98 リンク層の HELLO + PING/PONG。**`make kernel-lgy98-link` (LGY98_FLAG_LINKTEST) でビルドしたカーネルでのみ**走る。判定は `tools/net_l0_test.py` が `link_*` グローバルを `/api/mem` で読む | LINKTEST 版の毎起動 |
| [`tools/gui_gate.py`](../tools/gui_gate.py) | ゲスト観測 (判定は人) | GUI のゲート操作列 (`v11` / `v12g1` / `v12g4` / `shot` / `click`)。**自動照合は `wab_relay` / `scrn_ymax` / `fault_generation` の 3 数値だけ**、他は PM が目で見る | 手動 |
| `tools/emu_agent/tasks/regress.txt` | ゲスト観測 (ローカル AI) | 配備後の定型回帰 — kselftest カウンタ / `klibc_test` / `alloc_demo` / `ring3_fault` で落ちてもシェルが生きている / パイプライン 2 本 / screenshot。**モデルは観測を転記するだけで合否は人** | 手動 (スキル `os32-local-ai`) |
| `tools/emu_agent/tasks/v86_dos.txt` | ゲスト観測 (ローカル AI) | V86 の DOS 起動。`dos5hd.nhd` / master は 2026-09-10 に削除済みで、再作成は FORMAT から 30 分 | 手動 |

CI の静的ゲート (`.github/workflows/check.yml`、os32-v3 で 2026-09-30 に作り直し) が走らせるのは
生成表 (`build/out/TESTS.md`) の「CI ○」の `make <target>` (yml の行から生成) と、CI 専用の「生成物が `sdk/kapi.json` と
一致するか」(`gen_kapi.py` / `kapi_rust_gen.py` を回して `git diff --exit-code` で 5 ファイル)。
クロスコンパイラと rustc が無い状態で rc=0 になる検査だけを載せているので、末尾で `i386-elf-gcc` を
使うホスト試験 (R6) と rustc が要るもの (R7) は本体ビルドの `build.yml` の `make check-fast` が回す
([08_build.md §8-6](08_build.md))。**`check.yml` の成功を GUI やカーネルの動作確認とは扱わない**。
<!-- /manual:outside -->

## 5. 分類 (重複 / 不要 / 弱い / 穴)

<!-- manual:findings -->
2026-09-14 の読み取り専用調査 (基点 `fac0d89`) の結論。**採否は未決**で、PM / ユーザーが
決める前提の材料。根拠の行番号はその時点のもの。

### (a) 重複 — 同じ契約を 2 か所以上で守っている

| a | 重複 | 根拠 | 残す案 |
|---|---|---|---|
| **a1** | `multiapp_model_host.c` と `multiapp_impl_host.c` | `test_multiapp_impl.py` の docstring:「K5a の `multiapp_model_host.c` は状態機械を**手で書いた**模型。こちらは**出荷するカーネルのソース**をコンパイルして**同じ番号の検査**に掛ける」。ケース関数名を突き合わせると model の 19 件中 **17 件が impl に同名で存在**。impl のみ 10 件、model のみ 2 件 (`case_launch_pending_parks` / `case_wait_key_joins_the_round`) | **impl を残す**。model 固有の 2 件を impl へ移してから model (1,438 行 / 62KB) を撤去する。設計文書としての価値は `multiapp_model_tdd.md` に残る |
| **a2** | `check-term-model` / `check-term-render` の `cargo test` と直後の `cargo check --lib` | `cargo test` が同じクレートを既にビルドする。差は `cfg(test)` の有無だけ | `cargo check --lib` を落とす。「test 無しでも lib が通る」を本当に見たいなら残す判断もある — **PM/ユーザー決裁** |
| **a3** | ページング系ハーネスの重複 (試験内容ではなく**仕掛け**) | `test_paging_bounds.py` / `test_app_band_pde.py` / `test_highram_stage.py` / `test_memory_boot.py` が `kernel/paging.c` の**同じ 5 行の特権 asm 置換**を各自コピーしている | 試験そのものは別契約なので統合しない。置換だけを 1 つのヘルパへ |
| **a4** | `kselftest.c` の `test_con_sink` / `test_kbd_inject` / `test_launch` / `test_tramp_user_str` / `test_db_v50` / `test_app_band_pde` と、対応するホスト試験 | グループ名とホスト試験の対象ファイルが一致 | **重複として扱わない**。ホスト試験は網羅、kselftest 側は**煙試験**で、実機で毎起動踏むという別の価値がある。ただし 1 表明のものは (c4) 参照 |

### (b) 不要 — 守っている契約がもう無い / 到達しない

| b | 対象 | 根拠 | 判断 |
|---|---|---|---|
| **b1** | `tools/tests/paging_rebuild_host.c` (39 `CHECK()`、5 モード) が `make check` から**到達しない** | `test_paging_bounds.py` は `--rebuild` を `choices=[nonmaster, rollback, sparse, attrs, final]` で受け、付いたときだけ `paging_rebuild_host.c` を選ぶ。`check-memory-host` は引数なしで呼ぶ | 「不要」ではなく**死んだ被覆**。5 モードを足すか、意図的に手動なら票に書く。**PM 決裁** |
| **b2** | `check-privileged` が構造上落ちない | findings があっても `--strict` でなければ `return 0`。既定の根拠文は「**現在 userland は CPL=0 で動作**。リング3 導入時に `--strict` でゲートする」。しかし `CLAUDE.md` は「External programs run at CPL=3 with their own page directory」と書いており、リング3 は既に入っている | 根拠文が現状と矛盾。`--strict` へ上げる / `make check` から外す / 現 offender を票に書いて例外表を持つ、のいずれか。**PM 決裁** |
| **b3** | 撤去済み機能の試験 — **見つからなかった** | T5b (常駐表示パネル) は `1c98613` で撤去済みだが `wm_tests.rs` に `panel` の語は 0 件。hermes の語は `tools/` `userland/` のコードに 0 件。`t5a_display` も生きた依存 | **撤去候補なし** |

### (c) 弱い — 構造ガードだけで振る舞いを見ていない

| c | 対象 | 何が弱いか | 振る舞いの試験にするには |
|---|---|---|---|
| **c1** | [`tools/tests/test_gui_button_dispatch.py`](../tools/tests/test_gui_button_dispatch.py) | docstring が自認:「**挙動試験ではない。**」中身は `arm.find("widget::on_button_down") > arm.find("MOUSE_BTN_LEFT")` という**文字位置の比較**。`if` を `match` に書き換える / ガードを別関数に括り出す / 定数名を変える、のどれでも黙って通る (または黙って落ちる) | `libos32gui` には既に `host_tests/` クレートがある。`app.rs` の `on_event` を `#[path]` で取り込み、`GuiEvent { kind: GUI_EV_BUTTON, button: MOUSE_BTN_RIGHT }` を 1 つ流して贋物で数える。2〜3 ケースで置き換えられる |
| **c2** | [`tools/tests/test_filer_normalize.py`](../tools/tests/test_filer_normalize.py) の 3 件目 | docstring は「両実装を**実ソースから抜き出して**ホストでビルドし突き合わせる」と書くが、**ビルドして走るのは Rust 側だけ**。C 側は `fs/vfs.c` の窓を切って 3 つの文字列を `assertIn` するだけで、空白 1 つ変えれば落ち、`.` の畳み方を変えても文字列が残れば通る | `vfs_resolve_path` は純関数。`test_vfs_mount_dev.py` と同じやり方で `fs/vfs.c` を `#include` し、Rust 側と**同じ 10 ケース**を C でも回す |
| **c3** | [`tools/tests/test_hsync_protect.py`](../tools/tests/test_hsync_protect.py) | docstring:「**実体規則 (`sys_stat` の inode 比較) は `hsync.c` 側にあり、ここではコンパイルが通ることだけを見る**」。72 表明はすべて字句正規化と名前規則に掛かっており、最後の砦 (inode 一致判定) は 1 表明も無い | inode 比較部分を `.inc` に切り出し、`sys_stat` の贋物 (同 inode / 別 inode / stat 失敗) を 3 通り差し替えて「truncate に進むか進まないか」を見る |
| **c4** | `kernel/kselftest.c` の 1 表明グループ 4 本 — `test_kprintf` / `test_ring3_pd` / `test_map_user_keep` / `test_app_band_pde` | それぞれ `check()` 1 回。実質「その経路が呼べて落ちなかった」の確認で、境界も反例も踏んでいない | `test_app_band_pde` はホスト側が 107 表明で覆っているので実機側は「境界の 1 点だけ」で妥当とも言える。**削除でも増強でもなく、意図の明文化**が要る |

### (d) 守るべきなのに試験が無い契約 (目についたもの。網羅ではない)

| d | 契約 | 現状 |
|---|---|---|
| **d1** | `hsync` が実体 (inode) で `settings.db` を守る — 契約 D0 の最後の砦 | ホスト試験なし (c3 と同じ穴) |
| **d2** | `st_dev` / `st_ino` をゲストで観測する | 観測手段が無い。`vfs_mount_dev_host.c` の 2 ケースがホストだけの根拠 |
| **d3** | G9 — 2 本以上のアプリを同時に ready にしたときの pick | **実機観測の手段が無く未実施**。ホスト模型を受入とした |
| **d4** | `libos32gui` のウィジェット描画と WM 配送 | ホスト試験は `cfgro` wrapper だけ。描画・ジャンプ表・WM は「動かさない」と `Cargo.toml` が明記。Button 配送は構造ガード 1 本 (c1) |
| **d5** | ネットワーク L0〜L3 / M2〜M4 の回帰 | `make check` にも CI にも無く、`host_agent.py` の起動 + LINKTEST カーネルの配備 + 実機が要る。合否は各スクリプトの `RESULT:` 行 |
| **d6** | T5b 撤去で消えた 4 本の書き直し | `wm_tests.rs` には既に 4 本が揃っている。**残件記述が古い可能性が高い** — PM が確認して閉じるのが先 |
<!-- /manual:findings -->

## 6. 改善提言

<!-- manual:recommend -->
2026-09-14 の調査が出した案。**採否は未決** (PM / ユーザー決裁)。

### 削除候補

| 案 | 対象 | 根拠 | 前提 |
|---|---|---|---|
| **R1** | `tools/tests/multiapp_model_host.c` (1,438 行 / 62KB) と `check-multiapp-model-host` からの `test_multiapp_model.py` | (a1)。19 ケース中 17 ケースが `multiapp_impl_host.c` に**同名で**存在し、impl は出荷する `exec/appslot.c` をそのまま走らせる | 先に model 固有の 2 件を impl へ移す。設計の記録は `multiapp_model_tdd.md` に残る |

**削除を勧めないもの**: T5b / hermes 由来の残骸は**見つからなかった** (b3)。
`t5a_display` も `libos32term` / `libos32term_render` も生きた依存。

### 統合候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R2** | `check-term-model` / `check-term-render` の `cargo check --lib` を落とす | (a2)。直前の `cargo test` が同じクレートをビルドする |
| **R3** | ページング系 4 本の特権 asm 置換を 1 つのヘルパへ | (a3)。**試験は統合しない** — 守る契約が別々 |

### `make check` から外して手動に落とす候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R4** | SQLite の amalgamation を毎回コンパイルする 4 本 (`check-cfg-host` / `check-db-v50-host` / `check-install-recover-host` / `check-sqlite-groups-host`) を `make check-slow` のような別ターゲットへ分ける | `lib/sqlite3/sqlite3.c` は **267,757 行**を 4 回。**実行時間は測っていない**ので、分割の是非は PM が 1 回計測してから決める |
| **R5** | NP21/W ホスト道具の 4 本 (`test_np21w_ini` / `_ini_live` / `_trial` / `_transport`) を `check-emu-tools` として分離 | `check-tools-host` は OS32 の契約と NP21/W の ini 道具 ([D2] の PM 専用道具) を同じターゲットに混ぜている。**分離の是非も計測次第** |

### CI に足すべきもの

| 案 | 対象 | 根拠 | 注記 |
|---|---|---|---|
| **R6** | rustc **不要**の C / Python ホスト試験を CI へ (`check-con-sink-host` `check-kbd-inject-host` `check-launch-host` `check-ring3-str-host` `check-sh-launch-host` `check-sh-shell-host` `check-vfs-fd-sqlite-host` `check-vfs-mount-dev-host` `check-db-owned-host` `check-settings-protect-host`) | カーネルの契約 (con_sink / kbd_inject / launch / ring3_str / FD リース / mount エンコード) は**どれも CI で走っていない**。これらは `gcc` と `python3` だけで動く | **ただし全部が末尾で `i386-elf-gcc` のクロスコンパイル確認をする**。CI にクロスコンパイラが無いので、**そのステップだけ切り離せる形にする改修が要る** |
| **R7** | `check-gshell-host` / `check-gui-host` / `check-t5a-host` / `check-term-*` を CI に足す | **rustc が要る**。「CI 成功を GUI の動作確認とは扱わない」 | `actions/setup-rust` 系を 1 ステップ足せば届く。`test_filer_normalize.py` は `rustup toolchain list` の**先頭**を使うので、CI では toolchain が 1 本であること |

### 被覆を足す候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R8** | `paging_rebuild_host.c` の 5 モードを `check-memory-host` に足す (または「手動専用」と票に書く) | (b1)。39 表明が現在どの自動ゲートからも到達しない |
| **R9** | `test_gui_button_dispatch.py` を `libos32gui/host_tests` の贋物試験へ移す | (c1)。足場は既に `host_tests/src/fake.rs` にある |
| **R10** | `hsync` の inode 比較を `.inc` に切り出してホスト試験へ | (c3) / (d1)。契約 D0 の最後の砦が現在ノーガード |

### 文書の整理

| 案 | 対象 | 根拠 | 状態 |
|---|---|---|---|
| **R11** | [`gui/v13/PLAN.md`](tasks/gui/v13/PLAN.md) の「残件: パネルに依存しない形で書き直す」を確認して閉じる | (d6)。`wm_tests.rs` に 4 本が既にある | 未決 |
| **R12** | 参照されていない `*_tdd.md` を対応する試験から 1 行リンクする | 削除ではない。証跡としては有効なのに、どこからも名前で引かれていなかった | **大部分は解消**。生成表 (`build/out/TESTS.md`) の「記録」列が語幹の一致 (規則 3) で引くので、`app_band_pde` / `boot_splash_native` / `device_reservation` / `highram_stage` / `memory_boot` / `paging_bounds` / `pgalloc_range` / `pgalloc_model` がターゲットから辿れる。§ 追記も参照 |

**生成表 (`build/out/TESTS.md`) に出てこない `*_tdd.md` (49 本中 10 本、2026-09-15 時点)**

| ファイル | 出てこない理由 |
|---|---|
| `gui_review_20260910_tdd.md` / `gui_review3_…` / `gui_review4_…` | 対応する `check-*` ターゲットが無い (レビュー往復の記録で、自動試験に紐づいていない) |
| ~~`physmem_synthetic_source_tdd.md`~~ | 2026-09-16 に `physmem_tdd.md` へ改名して解消 (語幹 `physmem` で `test_physmem.py` に当たる) |
| `gshell_buttonup_tdd.md` / `k5b_gshell_tdd.md` | `check-gshell-host` は `integration.py` を呼ぶだけで、語幹も `記録:` も持たない |
| `k6_ram_tdd.md` | K6-RAM の記録。`check-memory-host` の `test_highram_stage.py` が持つのは `highram_stage_tdd.md` のほう |
| `s4_tdd.md` | 票 S4-W は `check-gshell-host` (Rust) の側。上と同じ理由 |
| `s5_tdd.md` / `t8_tdd.md` | それぞれ `test_cfg.py` / gshell の中で触れられているが、冒頭 40 行の外なので規則 1 に掛からない |

**直し方は 2 つとも規則の側ではなく情報源の側**: 試験スクリプトの冒頭 40 行に
`記録: tools/tests/<x>_tdd.md` を 1 行足すか、`build/*.mk` のターゲット直前の
コメントに書く。どちらも生成器が拾う。試験本体には手を入れていないので、この
10 本は**このコーダーの範囲では未解消**として残す ([V4])。
<!-- /manual:recommend -->

## 7. 未読 / 調べていないこと

<!-- manual:unread -->
2026-09-14 の調査が**読んでいない**もの。表の空欄と混同しないこと。

- **`apps/` と `game/`** — サブモジュールで、調査した worktree では空だった。
  「`apps/` の filer のホスト試験」は読めていない。in-tree の
  `userland/rust/filer/host/model_tests.rs` (3 件) は読んだ。
- **実行時間** — 1 本も実行していないので全て未計測。R4 / R5 の判断には実測が要る。
- **`test_deploy_protect.py` の中身** — クラス名だけ読んだ。8 クラスに対して
  `ReviewB1`〜`ReviewB9` / `Review2`〜`Review5` と **6 回のレビュー往復ぶんの層**が
  重なっている (35 クラス)。同じ配備 4 系統を何度も踏んでいる可能性があるが、
  **本文を読んでいないので重複とは断定しない**。持ち主が読んで判断すべき最有力候補。
- **`cfg_host.c` (3,778 行 / 1,173 表明) の中身** — ケース名リストは読んだが本文は未読。
  この表で最大の試験なので、重複の有無は別途。
- **`docs/tasks/**/TASK_*.md` の受入条件の全件突き合わせ** — (d) は「目についたもの」だけで、
  票の受入条件を端から照合してはいない。
<!-- /manual:unread -->
