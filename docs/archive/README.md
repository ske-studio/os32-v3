# docs/archive/ — 受入完了した票の置き場

**現行仕様 (運用の正典)。** 2026-09-16 開設。

`docs/tasks/` は「いま動いている作業」の棚であってほしいが、票は受入が済んでも消せない
(なぜその実装になったかの根拠であり、あとから仕様を問われるのはたいてい完了した票の中身)。
両方を満たすために、**受入完了して参照頻度が下がった票はここへ落とし、索引の行は残す**。
`docs/tasks/` を開いたときに目に入るのは進行中のものだけになり、完了した票は
`docs/INDEX.md` から 1 段深いところで生き続ける。

## 籠の切り方

領域 + 版で 1 つ。いま在るもの:

| 籠 | 中身 |
|---|---|
| `gui_v11/` | GUI シェル v1.1 の票 12 本 |
| `gui_v12/` | GUI シェル v1.2 の票 5 本 |
| `gui_v13_reviews/` | GUI シェル v1.3 のレビュー記録・クロスリンク・途中保存 |
| `settings/` | 設定レジストリの受入完了票 (S2 / S3 / S3I2 / S4 / S5、2026-09-29 に S0 / S6 / S6P と S0 の計画・基盤設計を追加) |
| `network/` | Host Services の票 (N0〜N4) |
| `kernel_v2/` | カーネル 2.0 の完了記録 8 本 |
| `shell/` | hsync H1〜H4・B8 (FS_TYPE)・シェルの切り詰め・終了コードの票 7 本 (2026-09-29 に移動) |
| `test/` | ゲスト試験ランナー 3 段 (FSTAT_REDIR / TEST_RESULT / TEST_RUNNER) |
| `tools/` | キー注入 (KEY_INJECT)・変異試験の並列化 (CHECK_MUT_PARALLEL) |
| `kernel_v21/` | v2.1 までに直したカーネル層の不具合の票 6 本 (KSTACK_USER / EXT2_EMPTY_NAME / KAPI_DATA_FIELDS / VFS_FD_PATH / KAPI_OUTPUT_GUARD / DB_ERRSTR) |
| `gui_v13/` | GUI シェル v1.3 の票 15 本と監査 (AUDIT_2026-09-10)。計画 `PLAN.md` と `MEMORY_BUDGET.md` は `tasks/gui/v13/` に残る |
| `gui_v14/` | GUI 1.4 のエディタ GUI 版 (TASK_EDIT_GUI) |
| `realhw_v21/` | v2.1 で実機 Ra266 に届いた票 (FDC / SERIAL_VFAST / HDD_INSTALL / SERIAL_HOSTFS / ATAPI_TIMEOUT)、PEGC_RA266_TIMING (完了記録、正典は `tasks/realhw/TASK_PEGC480_REALHW.md`)。実機の回の手順 3 本 (CHECKLIST_2026-09-24〜26、09-26 は v2.1 の判定の記録) は**実機作業の日次記録として os32 側に残した** (fork で持ってこなかった。[FORK_PLAN §1-2](../tasks/v3/FORK_PLAN.md)) |
| `v21/` | fork 時 (2026-09-30、[FORK_PLAN §4 J4](../tasks/v3/FORK_PLAN.md) = (a)) に `docs/tasks/` から落とした v2.1 までの受入完了の設計記録 48 本。元のディレクトリ名のまま: `sqlite/` (9)、`tilemap/` (8)、`v86v2/` (15 — md 12 + `data/` の json 3)、`boot_reform/` (8)、`wintree_port/` (1)、`libasset/` `libecs/` `libinput/` `libmath/` `libtext/` (各 1)、直下に `cross_compiler_rebuild.md` `ext2_dind_debug.md`。状態行の無かった 39 本には「完了記録 (os32 での最終コミット日)」を足した (D19、移動でいじった本文はこの 1 行だけ) |
| `portability/` | kstring の速度実測 (TASK_KSTRING_BENCH) |
| `v3/` | v3 で受入完了・完了記録になった票 (2026-10-02): T0 の TASK_C11_MIGRATION、TASK_CLANG_CHECKS、TASK_EXT2_ERRORS_INVESTIGATION、T1 の TASK_T1_LEDGER、策定時の PLAN・U6_PENDING_REVIEW・V3_PLAN_DRAFT、TASK_T2D_T2H から切り出した T2d の結果 (TASK_T2D_RESULTS)。進行中・受入待ちの v3 票 (V3_PLAN・TASK_MEMMAP_V3・T2・T3 以降・HAL_WIRING・PCM_CS4231 など) は `tasks/v3/` に残る |
| `agents/` | 引き継ぎ (HANDOVER_v14 / 2026-09-16 / 09-18 / 09-22)・体制の快照 (RETROSPECTIVE_2026-09-09)・Hermes の最上位プロンプト (SOUL.md、撤収済み・効力なし) |
| 直下 | 領域に属さない単発の記録 (`REFACTORING_PLAN.md` `ROADMAP_v1.0.md` `TEST_INVENTORY_2026-09-14.md` `debug_kcg_load_font.md`) |

**計画・設計・契約・索引は落とさない。** `PLAN.md` `DESIGN.md` `CONTRACTS.md`
`API_CONTRACTS.md` `TASKS.md` の類は完了後も現行仕様として読まれ続けるので
`docs/tasks/<領域>/` に残る。落とすのは「その作業をやり切った」票だけ。

## 移し方

手で `git mv` しない。**`tools/move_docs.py`** が `git mv` と参照の書き換えを 1 手でやる。

```bash
python3 tools/move_docs.py --into docs/archive/<領域> docs/tasks/<領域>/TASK_X.md ... --dry-run
python3 tools/move_docs.py --into docs/archive/<領域> docs/tasks/<領域>/TASK_X.md ...
```

リポジトリ中の `.md` を舐めて、動いた文書を指す相対リンクと、地の文の
ルート相対パス言及 (票と `tools/tests/*_tdd.md` が互いを指す書き方) を追従させる。
動いた文書自身の中のリンクは、深さが変わるので全部引き直す。
`--dry-run` で書き換え一覧だけ出せるので、**先に見てから当てる**。

移したあとに回すもの (3 つとも `make check` の列に入っている):

```bash
python3 tools/check_docs_links.py              # リンクと見出しアンカー
python3 tools/check_docs_orphans.py            # 索引から辿れなくなっていないか
python3 tools/gen_tests_inventory.py --write   # TESTS.md の「票」列を引き直す
```

`docs/INDEX.md` の索引行と、上の籠の表は人が書く。

## 移したあとも守ること

* **`check-docs-links` が通ること** — 相対パスが 1 段ずれるのはいちばん起きやすい
  壊し方なので、`docs/archive/` はリンク検査から除外していない
  (`tools/check_docs_links.py` の docstring)。
* **索引には残すこと** — 票の行を `docs/INDEX.md` から消さない。リンク先が
  `tasks/…` から `archive/…` に変わるだけ。`check-docs-orphans` は
  `docs/INDEX.md` を唯一の起点に辿るので、消すとそのまま孤児になる。
* **本文は書き換えないこと** — 移動でいじってよいのはリンクだけ。完了記録を
  あとから直すと、当時の判断の記録でなくなる。続きが要るなら新しい票を立てる。
* **票は票のまま** — `tools/tests/*_tdd.md` が根拠として指す票がここへ来ても
  参照は生きる (`check_docs_orphans.py` の起点集合は `docs/archive/**` を含む)。
