# 試験項目の棚卸し (2026-09-14)

> **正典は [`docs/TESTS.md`](../TESTS.md) へ移行 (2026-09-15)。** 本書は当時の快照。一覧は
> `tools/gen_tests_inventory.py` が `build/*.mk` から生成する形になり、生成できない判断
> (§2 の分類 / §3 の R1〜R12 / §4 の未読) は `docs/TESTS.md` の `<!-- manual:… -->` 区間へ移した。

> PM 注記 (2026-09-14): 発見 5 (PLAN.md の残件) は `1b56db3` で解消済み (4 本は `89ff220` で復元されていて票の行だけ古かった。反対側の被覆 4 本を追加)。本書はユーザーの「v2 前に再考する点」の材料で、推奨 R1〜R12 の採否は未決。


ユーザー指示 (2026-09-10、[`gui/v13/PLAN.md`](../tasks/gui/v13/PLAN.md) 26〜28 行目)
「テスト内容が必要ない項目がある」に応える読み取り専用の調査。
**コード・試験・Makefile は 1 行も変えていない。試験は 1 本も実行していない**
(数字はすべてソースを数えたもの。実行時間は測っていないので「未計測」と書く)。

基点 commit **`fac0d89`** (worktree `agent-ac2bc17b5785c1693`)。
発注書にあった `995bb19` はこの worktree の HEAD ではない (`.git/refs` で確認)。
PM / ユーザーが削除・統合を決める前の材料であり、この文書自体は決定ではない。

## 0. 数え方と読み方

- **件数**の意味は列ごとに違う。C のホスト試験は `check()` / `CHECK()` の呼び出し数 (= 表明の数、
  マクロ定義の 1 行は引いてある) と、`case_*` / `t_*` の関数が切ってある場合はその数。
  Rust は `#[test]` の数。Python は `def test*` の数、または `CASES` リストの長さ。
- **種類**は 5 つ: 静的整合性 / ホスト模型 / 実物ソースを #include / ゲスト観測 / 起動時自己試験。
  「実物ソースを #include」は、出荷するソースをそのままホストで走らせる形 (模型ではない)。
- **実行時間**は測っていない。分かるのは「SQLite の amalgamation (`lib/sqlite3/sqlite3.c`、
  **267,757 行**) を毎回コンパイルする」など、ソースから読める重さの根拠だけ。
- `make check` は `build/sdk.mk:239` の 1 行が正典 (**31 ターゲット**)。
  CI (`.github/workflows/check.yml`) が走らせるのは **6 ステップ**だけで、うち 5 つが
  `make check` と重なる (`check-kapi-version` / `check-constraints` / `check-shlib` /
  `check-gui-proto` / `check-ne2000-ring`)。残る 1 つは CI 専用の「生成物が
  `sdk/kapi.json` と一致するか」(`git diff --exit-code`)。
- **`make check` は単独では走らない**: `check-manifests` は `make all` の成果物を見る
  (`build/sdk.mk:89` のコメント)、`check-privileged` は `userland/**/*.o` が無いと
  exit 2 (`tools/check_privileged.py:124`)。
- **未読**: `apps/` と `game/` はサブモジュールで、この worktree では**空**
  (`.gitmodules` にあるが未チェックアウト)。指示にあった「`apps/` の filer のホスト試験」は
  読めていない。in-tree の filer (`userland/rust/filer/`) は読んだ。

---

## 1. 一覧表

### 1-A. 静的整合性 (7 行)

| # | 名前 | 種類 | 何を守っているか | 件数 | 時間 | `make check` | CI |
|---|---|---|---|---|---|---|---|
| 1 | `tools/check_kapi_version.py` (155 行) | 静的整合性 | [ABI1] — KAPI 版数の唯一の情報源は `sdk/kapi.json`。生成ヘッダと手書きドキュメントの版数がずれると「どれが本当の版か」が分からなくなる (docstring) | 照合先 4 箇所 | 未計測 | ○ `check-kapi-version` | ○ |
| 2 | `tools/check_manifests.py` (524 行) | 静的整合性 | [V2] — ①配備定義にあるのにビルドされない ②`app.conf` のキーが実ターゲットと不一致 ③ビルドされるのに配備定義に無い ④`gfx_init` を呼ぶのに `app.conf` に `gfx` 宣言が無い (票 T8 D1a) の 4 種 | 4 種の食い違い | 未計測 (`make all` 前提) | ○ `check-manifests` | × |
| 3 | `tools/check_constraints.py` (98 行) | 静的整合性 | 規則の正典 `docs/CONSTRAINTS.md` と参照側 (`CLAUDE.md` / `SOUL.md`) の ID ずれ 3 種 (正典にあり参照に無い / 参照にあり正典に無い / 採番重複) | 3 種 | 未計測 | ○ `check-constraints` | ○ |
| 4 | `tools/check_privileged.py` (155 行) | 静的整合性 | CPL=3 で `#GP` する特権命令が `userland/**/*.o` に残っていないか。**既定は警告のみで必ず exit 0** (`main()` 末尾 `return 0`)。`--strict` でのみゲートになる | offender 列挙 | 未計測 | ○ `check-privileged` (既定 = 非ゲート) | × |
| 5 | `tools/check_gui_proto.py` (232 行) | 静的整合性 | GUI 共有プロトコルの C ⇄ Rust 照合 — `os32_gui_shared.h` と `gui/proto.rs` の `GUI_*` / `OS32_ERR_*` 定数の値、`#[repr(C)]` 構造体の大きさとフィールド列。v1.2 の G0 (契約凍結) のゲート | 定数 + 構造体の全件 | 未計測 | ○ `check-gui-proto` | ○ |
| 6 | `tools/mkshlib.py --check` | 静的整合性 | `libos32gui.shlib` の番号表 (ジャンプ表) がずれていないこと。ずれると shlib 経由の呼び出しが別関数に飛ぶ | 番号表全件 | 未計測 | ○ `check-shlib` | ○ |
| 7 | 生成物の同期 (CI 専用) | 静的整合性 | [ABI1] 生成ファイルを手で触らない — `gen_kapi.py` / `kapi_rust_gen.py` を回してから `git diff --exit-code` で 5 ファイル | 5 ファイル | 未計測 | × | ○ |

### 1-B. ホスト試験 (`make check` 経由。50 行)

| # | 名前 | 種類 | 何を守っているか | 件数 | 時間 | `make check` | CI |
|---|---|---|---|---|---|---|---|
| 8 | `tools/tests/ne2000_ring_test.c` | 実物ソースを #include (`drivers/ne2000_ring.c`) | NE2000 受信リングの番地計算。I/O の実動作試験は代替しない (`build/kernel.mk:62` のコメント) | 30 `expect()` | 未計測 | ○ `check-ne2000-ring` | ○ |
| 9 | `userland/libos32term/tests/model.rs` | ホスト模型 | 端末表示モデル (固定記憶) の状態遷移。**ゲストのクロスリンク・描画・CUI 統合の検証ではない** (`build/sdk.mk:109`) | 24 | 未計測 (rustc 必須) | ○ `check-term-model` | × |
| 10 | `userland/libos32term/tests/stream.rs` | ホスト模型 | 同上 — バイト列 → セル列の流し込み | 8 | 未計測 | ○ 同上 | × |
| 11 | `userland/libos32term/tests/clip.rs` | ホスト模型 | 同上 — 矩形クリップ | 7 | 未計測 | ○ 同上 | × |
| 12 | `userland/libos32term/tests/utf8.rs` | ホスト模型 | 同上 — UTF-8 の切り出しと桁幅 | 8 | 未計測 | ○ 同上 | × |
| 13 | `userland/libos32term_render/tests/render.rs` | ホスト模型 | 純粋描画アダプタ。**人工 glyph であり実 ROM 描画の検証ではない** (`build/sdk.mk:114`) | 21 | 未計測 | ○ `check-term-render` | × |
| 14 | `userland/libos32term_render/tests/rework.rs` | ホスト模型 | 同上 — 再描画範囲の再計算 | 11 | 未計測 | ○ 同上 | × |
| 15 | `userland/libos32term_render/src/rework_tests.rs` | ホスト模型 | 同上 (クレート内 `#[cfg(test)]`) | 7 | 未計測 | ○ 同上 | × |
| 16 | `userland/rust/t5a_display/host_tests/` (src 12 モジュールを `#[path]` 取り込み) | 実物ソースを #include | 端末アプリ T5a の状態・座標・所有権。**`guest.rs` の実行は含まない** (`build/sdk.mk:119`)。内訳: launch 11 / prompt 13 / sink 10 / state 7 / session 5 / paint 3 / status 3 / view 3 / boundary 2 / inject 2 / input 2 / storage 1 | 62 | 未計測 (rustc 必須) | ○ `check-t5a-host` | × |
| 17 | `tools/tests/multiapp_model_host.c` | ホスト模型 (手書き) | K5a (4 アプリ、契約 T2a) の設計を純粋状態機械で固定したもの。**カーネル実装の正しさは何も言わない** (`build/sdk.mk:126`) | 19 ケース / 125 `check()` | 未計測 | ○ `check-multiapp-model-host` | × |
| 18 | `tools/tests/multiapp_impl_host.c` | 実物ソースを #include (`exec/appslot.c` `exec/launch.c` `kernel/kbd_inject.c` `fs/fd_redirect.c`) | K5b — 出荷する AppSlot を K5a と**同じ番号の検査**に掛ける + OP_WAIT 印 (C5/C6) + WAIT_KEY (K7 第 2 park 点) | 27 ケース / 413 `check()` | 未計測 | ○ 同上 | × |
| 19 | `tools/tests/owner_reclaim_host.c` | 実物ソースを #include (`fs/fd_redirect.c` `fs/pipe_buffer.c` `kernel/shm.c`) | K5a D3 — ID 2 を畳んだとき ID 2 のリダイレクト / パイプ / SHM **だけ**が解放される | 27 `check()` | 未計測 | ○ 同上 | × |
| 20 | `tools/tests/test_physmem.py` | 実物ソースを #include (`kernel/physmem.c`) | 物理レンジ模型。合成入力だけで、実機 RAM の証拠にはしない (docstring) | 9 | 未計測 | ○ `check-memory-host` | × |
| 21 | `tools/tests/paging_bounds_host.c` | 実物ソースを #include (`kernel/paging.c` + `pgalloc.c`、特権 asm だけ置換) | ページング境界 (Phase 1)。シェルの 2 ヒープ (0x380000–0x3FFFFF) を present に保つ類の不変条件 | 95 `CHECK()` | 未計測 | ○ 同上 | × |
| 22 | `tools/tests/paging_rebuild_host.c` | 実物ソースを #include (同上) | PD 再構築の 5 モード (nonmaster / rollback / sparse / attrs / final) | 39 `CHECK()` | 未計測 | **×** — `test_paging_bounds.py` は `--rebuild` を付けたときだけこのハーネスを選ぶ (同 26 行目)。`check-memory-host` は引数なしで呼ぶ | × |
| 23 | `tools/tests/app_band_pde_host.c` | 実物ソースを #include (同上) | `docs/tasks/memory/APP_BAND_PDE.md` — アプリ帯が 4MB (PDE) 単位で伸びる | 107 `CHECK()` | 未計測 | ○ 同上 | × |
| 24 | `tools/tests/test_pgalloc_model.py` | 実物ソースを #include (`kernel/pgalloc.c` + `sys.c` + `exec/exec.c` の配置ヘルパ) | 物理アロケータと物理レンジ模型の結合。`exec_child_claim` は並行に書いた別式ではなく出荷コードを切り出す (docstring) | 12 | 未計測 | ○ 同上 | × |
| 25 | `tools/tests/pgalloc_range_host.c` | 実物ソースを #include (`kernel/pgalloc.c`) | A0 — 範囲付き pgalloc。`pgalloc_range_has_ram` 系 (POLICY_DEBUG §4-34 のデバイス窓の判断) | 48 `CHECK()` | 未計測 | ○ 同上 | × |
| 26 | `tools/tests/highram_stage_host.c` | 実物ソースを #include (`paging.c` `pgalloc.c` `sys.c`) | 高位 RAM の段階公開 (K6-RAM)。32/64 は模型上の MiB で、実行は実ターゲット ILP32 型 | 59 `CHECK()` | 未計測 | ○ 同上 | × |
| 27 | `tools/tests/memory_boot_host.c` | 実物ソースを #include (同上 + `kernel/kernel.c`) | 起動アダプタ — 8MB / legacy 構成で memory サブシステムが立ち上がる順序 | 71 `CHECK()` | 未計測 | ○ 同上 | × |
| 28 | `tools/tests/device_reservation_stage_host.c` (`test_device_reservation.py`) | 実物ソースを #include (`sys.c` / `pgalloc.c`) | デバイス窓の予約コア (`sys_reserve_top` 系)。デバイス I/O もマッピングもしない | 8 / 23 `CHECK()` | 未計測 | ○ 同上 | × |
| 29 | `tools/tests/sbrk_tier_host.c` | 実物ソースを #include (`exec/exec.c` の判定 3 関数 + `exec/appslot.c`) | CPL=3 プログラムの sbrk 物理「二段構え」(決裁 2026-09-11、票 K5B 作業 8) | 4 ケース / 39 `check()` | 未計測 | ○ 同上 | × |
| 30 | `tools/tests/boot_splash_native_host.c` | 実物ソースを #include (`gfx/gfx_core.c` `gfx/backend_pc98.c` `kernel/boot_splash.c`) | ネイティブ起動時の gfx ディスパッチが GUI 設定を保つ / 不正状態から後始末して再試行できる | 2 / 20 `CHECK()` | 未計測 | ○ `check-boot-splash-host` | × |
| 31 | `tools/tests/test_np21w_ini.py` | ホスト模型 | `tools/np21w_ini.py` — NP21/W ini のオフライン変換 ([D2] の道具)。CP932 のバイト列を壊さない、既存キーの書式を保つ | 49 | 未計測 | ○ `check-tools-host` | × |
| 32 | `tools/tests/test_np21w_ini_live.py` | ホスト模型 | `tools/np21w_ini_live.py` — 起動中インスタンスの ini 変更 ([D2])。PowerShell 経路は opt-in | 47 | 未計測 | ○ 同上 | × |
| 33 | `tools/tests/test_np21w_transport.py` | ホスト模型 (実パイプ) | trial のトランスポート終了処理。ライフサイクルスクリプトもエミュレータも触らない | 8 | 未計測 | ○ 同上 | × |
| 34 | `tools/tests/test_np21w_trial.py` | ホスト模型 | `tools/np21w_trial.py` — 使い捨て trial ini の組み立て ([D2]、票 S3I2) | 31 | 未計測 | ○ 同上 | × |
| 35 | `tools/tests/test_nhd_deploy_failure.py` | 実物ソースを import (`tools/nhd_deploy.py`) | [V4] — 2026-09-10 の実障害 (POLICY_DEBUG §4-29) の再発防止。`cp: No space left` で exit 0「完了」と言わせない | 3 | 未計測 | ○ 同上 | × |
| 36 | `tools/tests/test_filer_normalize.py` | 一部ホスト実行 + 一部構造ガード | 2026-09-10 レビュー blocker — filer の `normalize_abs` が VFS の `vfs_resolve_path` と同じ正規形を出す (でないと同一ファイルへのコピーが `O_TRUNC` で元を消す) | 3 (うち Rust 側実行 2、C 側は文字列照合 1) | 未計測 (rustc 必須) | ○ 同上 | × |
| 37 | `tools/tests/test_filer_copy_abort.py` + `userland/rust/filer/host/model_tests.rs` | 実物ソースを #include (`filer/src/model.rs`) | 2026-09-10 レビュー指摘 — 中断時に自分が作った途中のコピー先を消し、既存の宛先は消さない | 3 | 未計測 (rustc 必須) | ○ 同上 | × |
| 38 | `tools/tests/test_gui_button_dispatch.py` | **構造ガード (自称「挙動試験ではない」)** | 契約 D4 — 右クリックでウィジェットの `on_click` を起こさない。`app.rs` の `GUI_EV_BUTTON` アームを正規表現で切り出し、配送呼び出しが `MOUSE_BTN_LEFT` 判定より**後ろの位置にある**ことを文字位置で見る | 11 `check()` | 未計測 | ○ 同上 | × |
| 39 | `tools/tests/test_emu_playbook.py` | ホスト模型 | `tools/emu_agent/playbook.py` — ローカル AI の行動検証・ループ制御・CLI。実ヘルパも LLM もエミュレータも触らない | 41 | 未計測 | ○ 同上 | × |
| 40 | `tools/tests/test_mk_settings_db.py` | ホスト模型 + 実 SQLite | 票 S0-T — 初期値 tsv → `settings.db` の決定性 (同内容 + 同 epoch なら同一バイト列)、DESIGN.md §3 のスキーマ、tsv 規則、mkpkg の欠損検出 | 45 | 未計測 (実 SQLite) | ○ 同上 | × |
| 41 | `tools/tests/test_mk_blank_nhd.py` | ホスト模型 | 票 S3I2-T 2b — `mk_blank_nhd.py` のジオメトリ。根拠は np21w-src `sxsihdd.h` の NHDHDR 512B と `sig_nhd` | 13 | 未計測 | ○ 同上 | × |
| 42 | `userland/gshell/host/wm_tests.rs` (128KB) | 実物ソースを #include (`gshell/src/*.rs` を `integration.py` が丸ごと取り込む) | gshell WM の配送・スケジューリング契約 — 上位 UI (taskbar / menu / modal / FEP) の押下-離しがアプリの次の押下を飲み込まない、4 アプリの pick / park / round bound、Ctrl+STOP の対象、full-screen 所有者、launch chain | 68 | 未計測 (rustc 必須) | ○ `check-gshell-host` | × |
| 43 | `userland/gshell/host/settings_tests.rs` (51KB) | 実物ソースを #include (`gshell/src/settings.rs`) | 票 S4-W — 設定ダイアログの状態機械、既定値への畳み込み、書き込み失敗で適用値を動かさない、pump/handler 文脈から DB を触らない | 23 | 未計測 | ○ 同上 | × |
| 44 | `userland/gshell/host/wm_composite_tests.rs` | 実物ソースを #include (`gshell/src/wm.rs`) | W-3 回帰 (`36ccf15`) — 背面窓の枠線・タイトル文字が前面のクライアント面に落ちない | 3 | 未計測 | ○ 同上 | × |
| 45 | `tools/tests/kapi_db_owned_host.c` | 実物ソースを #include (`kapi/kapi_db.c`) | F1 — DB 接続の所有者寿命。SQLite 側の失敗注入付き | 6 ケース / 64 `assert()` | 未計測 | ○ `check-db-owned-host` | × |
| 46 | `tools/tests/vfs_fd_sqlite_host.c` | 実物ソースを #include (FD 表) | F2a — FD リース。`vfs_fd_set_protect` (カーネル常駐 FD)、世代番号の枯渇、隔離、容量事前検査 | 15 ケース / 138 `CHECK()` | 未計測 | ○ `check-vfs-fd-sqlite-host` | × |
| 47 | `tools/tests/vfs_mount_dev_host.c` | 実物ソースを #include (`fs/vfs.c` `fs/ext2_vfs.c`) | POLICY_DEBUG §4-30 / MEMORY の回帰 — `mount(dev_id)` は `(dev_type << 8) \| unit`。種別を見ないと `fd0` が `hd0` に化けて同じパーティションを二重マウントする | 6 ケース / 51 `CHECK()` | 未計測 | ○ `check-vfs-mount-dev-host` | × |
| 48 | `tools/tests/ext2_read_bound_host.c` | 実物ソースを #include (`fs/ext2_file.c`) | POLICY_DEBUG §4-32 — `ext2_read_file` が端数ブロックで `max_size` を最大 1023B 溢れさせない (K5b-K 初回起動の「shell.bin load failed」の根本原因) | 4 ケース / 4 `CHECK()` | 未計測 | ○ 同上 | × |
| 49 | `tools/tests/fatfs_stat_host.c` | 実物ソースを #include (`fs/fatfs_vfs.c`) | 票 S3-K — FDD ブート (FF_USE_LFN 0) で `settings.db-journal` が 8.3 に収まらず `FR_INVALID_NAME` になる → KAPI v50 の hot journal 検査が IOERR を返す障害 | 11 ケース / 62 `CHECK()` | 未計測 | ○ 同上 | × |
| 50 | `tools/tests/sqlite_groups_host.c` | 実物ソースを #include (実 SQLite VFS + `lib/sqlite3/sqlite3.c`) | F2b — 接続専用 SQLite VFS の group 登録 / 容量 / 孤児 / VFS コールバック 11 本 | 32 ケース / 223 `CHECK()` | 未計測 — **`sqlite3.c` (267,757 行) を毎回コンパイル** | ○ `check-sqlite-groups-host` | × |
| 51 | `tools/tests/con_sink_host.c` | 実物ソースを #include (`kernel/con_sink.c` + `kernel/console.c`) | 票 K6C §2 — console シンクのリング (GUI 中の `console_write` / `shell_print` を端末モデルへ)。錠の中でしか head/tail/count を動かさない。同じソースが `i386-elf-gcc -Werror` でも通ること ([C1]) | 10 ケース / 106 `check()` | 未計測 | ○ `check-con-sink-host` | × |
| 52 | `tools/tests/kbd_inject_host.c` | 実物ソースを #include (`kernel/kbd_inject.c` + `con_sink.c` 同一翻訳単位) | 票 K7 §1 D2〜D5 — 打鍵注入の 256B リング。注入の権限は「con_sink の読み手 1 本」 | 6 ケース / 51 `check()` | 未計測 | ○ `check-kbd-inject-host` | × |
| 53 | `tools/tests/launch_host.c` | 実物ソースを #include (`exec/launch.c` + `exec/appslot.c`) | 票 T9 D3 — 起動要求表 (`launch_req` / take / report / poll)。要求者・子の所有・token の照合 | 9 ケース / 115 `check()` | 未計測 | ○ `check-launch-host` | × |
| 54 | `tools/tests/ring3_str_host.c` | 実物ソースを #include (`exec/ring3_str.c`) | 票 T9 §12 R1 — KAPI が CPL=3 へ**返す**文字列の置き場。カーネル帯の PTE に USER が無いので `sys_getcwd` がそのまま返すと `cd` / `pwd` が #PF → fault kill | 4 ケース / 20 `check()` | 未計測 | ○ `check-ring3-str-host` | × |
| 55 | `tools/tests/sh_launch_host.c` | 実物ソースを #include (`userland/shell/sh_launch.inc`) | 票 T9 D3a — `sh.bin` の起動待ち。DONE / FAILED / STALE / FULL の 4 経路と「待ちの間 `kbd_*` / `ime_*` を呼ばない」 | 7 ケース / 32 `check()` | 未計測 | ○ `check-sh-launch-host` | × |
| 56 | `tools/tests/sh_shell_host.c` | 実物ソースを #include (`sh_redraw.inc` `cmd_script.c`) | T9 実装レビュー blocker 2 件 — GUI 中は行再描画がコンソール座標に依存しない / `source` 中の `exit` が後続を止める (D2(d)) | 20 ケース / 111 `check()` | 未計測 | ○ `check-sh-shell-host` | × |
| 57 | `tools/tests/test_deploy_protect.py` (2,184 行 / 35 クラス) | ホスト模型 (temp dir + mock) | 票 S0-D / 契約 D0 — 配備 4 系統 (マニフェスト書き込み / HostDrv→NHD 丸写し / prune / NHD 丸ごと上書き) のどれも `/etc/settings.db*` を作らない・上書きしない・消さない。sudo / mount / mkfs は `FakeRun` が例外にして遮断 | 164 | 未計測 | ○ `check-settings-protect-host` | × |
| 58 | `tools/tests/hsync_protect_host.c` | 実物ソースを #include (`userland/system/hsync_protect.inc`) | 票 S0-D — `hsync` の**字句判定** (正規化 + 名前規則) が `settings.db*` を守る。**実体規則 (`sys_stat` の inode 比較) は `hsync.c` 側にあり、ここではコンパイルが通ることだけを見る** (docstring) | 72 `check()` | 未計測 | ○ 同上 | × |
| 59 | `tools/tests/kapi_db_v50_host.c` | 実物ソースを #include (実 SQLite + 実 VFS + 実 FD 表 + `kapi/kapi_db.c`) | 票 S0-K — KAPI v50 (`db_open_existing` / `prepare_only` / `bind_*` / `error_code`)。実装レビュー往復 1・2 の blocker 10 件を含む | 22 ケース / 409 `CHECK()` | 未計測 — **`sqlite3.c` を毎回コンパイル** | ○ `check-db-v50-host` | × |
| 60 | `tools/tests/cfg_host.c` (3,778 行、156KB) | 実物ソースを #include (実 SQLite + `userland/lib/cfg/*.c` + `userland/cmds/cfg.c`) | 票 S2-C — `libos32cfg` と `cfg` コマンド。tsv fixture も stdin 経由でホスト FS に触らない | 53 ケース / 1,173 `CHECK()` | 未計測 — **`sqlite3.c` を毎回コンパイル。この表で最大の試験** | ○ `check-cfg-host` | × |
| 61 | `userland/rust/libos32gui/host_tests/src/lib.rs` | 実物ソースを #include (`libos32gui/src/cfgro.rs`、C 側は贋物) | 票 S2-W — `os32gui_cfg_*` wrapper の分岐。open / begin / set / commit / close の呼び順、失敗の畳み方、OS scope の拒否。**描画も WM もジャンプ表も動かさない** (Cargo.toml のコメント) | 34 | 未計測 (rustc 必須) | ○ `check-gui-host` | × |
| 62 | `userland/rust/libos32gui/host_tests/tests/init_gate.rs` | 同上 | w29 — shlib 初期化前の呼び出しが `libos32cfg` を触らない | 1 | 未計測 | ○ 同上 | × |
| 63 | `tools/tests/install_recover_host.c` (1,775 行) | 実物ソースを #include (`install_recover.inc` + 実 SQLite + 実 VFS) | 票 S3-I — `install --recover-settings` / `--revert-settings`。媒体判定 / 三値スキャン / 印の門 / マスタ検査 / 承認しない経路 | 14 ケース / 322 `CHECK()` | 未計測 — **`sqlite3.c` を毎回コンパイル** | ○ `check-install-recover-host` | × |
| 64 | `tools/tests/install_fresh_host.c` | 実物ソースを #include (`userland/system/install.c` の `main`) | 票 S3I2-I — 新規インストール経路。`/kernel.bin` を読まない、`/hd0/boot/vmkernel.lz4` の長さ一致、IdeInfo 96B の表明、read/short write/mkdir/format/mount/sync/ide_write の失敗注入 | 12 ケース / 84 `CHECK()` | 未計測 | ○ `check-install-fresh-host` | × |

### 1-C. 起動時自己試験 (2 行)

| # | 名前 | 種類 | 何を守っているか | 件数 | 時間 | `make check` | CI |
|---|---|---|---|---|---|---|---|
| 65 | `kernel/kselftest.c` (546 行) | 起動時自己試験 | カーネルが**実際に使う**プリミティブ (`kstring_asm.asm` / `kmalloc.c` / `kprintf`) の境界ケース。外部プログラムの `klibc_test` は newlib を測るので代替にならない (ファイル冒頭)。結果は `kselftest_pass` / `kselftest_fail` に残り `kernel.map` 経由で読む | 88 `check()` / 17 グループ。内訳: `test_str` 15、`test_heap` 12、`test_resume_mark` 9、`test_mem` 8、`test_con_sink` 7、`test_db_v50` 6、`test_utoa` 5、`test_kbd_inject` 5、`test_abort_admit` 5、`test_gfx_owner` 3、`test_launch` 3、`test_tramp_user_str` 3、`test_con_sink_render_gate` 2、`test_kprintf` 1、`test_ring3_pd` 1、`test_map_user_keep` 1、`test_app_band_pde` 1 | 毎起動 | × (実機) | × |
| 66 | `link_selftest()` (`drivers/lgy98.c`) | 起動時自己試験 | LGY-98 リンク層の HELLO + PING/PONG。**`make kernel-lgy98-link` (LGY98_FLAG_LINKTEST) でビルドしたカーネルでのみ走る**。判定は `tools/net_l0_test.py` が `link_*` グローバルを `/api/mem` で読む | 10 往復 | 毎起動 (LINKTEST 版のみ) | × | × |

### 1-D. ゲスト観測 (`make check` 外。10 行)

| # | 名前 | 種類 | 何を守っているか | 件数 | 時間 | `make check` | CI |
|---|---|---|---|---|---|---|---|
| 67 | `tools/net_m2_test.py` | ゲスト観測 (実機 + 反射カーネル) | LGY-98 M2 — inject → 受信 → 反射送信 → capture。最短 / 奇数長 / 256B ページ境界前後 / 最大長 / 連続 10 フレーム | 5 種 | 未計測 | × `check-net-m2` | × |
| 68 | `tools/net_m2_test.py --during-cmd` | ゲスト観測 | M3 — CPL=3 プログラム常駐中に NIC IRQ がアプリの CR3 で配送される | 上と同じ 5 種 | 未計測 | × `check-net-m2-cpl3` | × |
| 69 | `tools/net_m4_test.py` | ゲスト観測 | M4 — リングを故意に飽和させてもドライバが wedge せず 100Hz ウォッチドッグで自己回復する | 2 項目 | 未計測 | × `check-net-m4` | × |
| 70 | `tools/net_l0_test.py` | ゲスト観測 | L0 — Stop-and-Wait の HELLO 確立 + 10/10 往復 | — | 未計測 | × `check-net-l0` | × |
| 71 | `tools/net_l1_test.py` | ゲスト観測 | L1 — 絶対値 WINDOW/Credit。200 DATA を溢れさせず順序どおり、NIC drop 0 | — | 未計測 | × `check-net-l1` | × |
| 72 | `tools/net_l2_test.py` | ゲスト観測 | L2 — 128KB を 8KB バッファで消費、Go-Back-N の欠落回復 | — | 未計測 | × `check-net-l2` | × |
| 73 | `tools/net_l3_test.py` | ゲスト観測 | L3 — Host Services の GET (ストリーム) / 404 / TIME (RPC) | 4 項目 (実 HTTP は参考) | 未計測 | × `check-net-l3` | × |
| 74 | `tools/gui_gate.py` | ゲスト観測 (判定は人) | GUI のゲート操作列。`v11` (gui_demo のドラッグ / 重なり / クリック)、`v12g1`、`v12g4`、`shot`、`click`。**自動照合は `wab_relay` / `scrn_ymax` / `fault_generation` の 3 数値だけ**、他は PM が目で見る (docstring) | 3 台本 + 2 デバッグ | 未計測 | × | × |
| 75 | `tools/emu_agent/tasks/regress.txt` | ゲスト観測 (ローカル AI) | 配備後の定型回帰 — kselftest カウンタ / `klibc_test` / `alloc_demo` / `ring3_fault` で落ちてもシェルが生きている / パイプライン 2 本 / screenshot。**モデルは観測を転記するだけで合否は人 (ファイル冒頭)** | 6 タスク | 未計測 | × | × |
| 76 | `tools/emu_agent/tasks/v86_dos.txt` | ゲスト観測 (ローカル AI) | V86 の DOS 起動。**MEMORY の記録では `dos5hd.nhd` / master は 2026-09-10 にユーザーが削除済み** (再作成に FORMAT から 30 分) | — | 未計測 | × | × |

**一覧表 = 76 行** (1-A 7 / 1-B 50 / 1-C 2 / 1-D 10、および 1-B 内の #22 は `make check` 到達なし)。

### 1-E. 参考: `tools/tests/*_tdd.md` (35 本) は試験ではなく RED→GREEN の記録

うち **10 本はどこからも参照されていない** (試験・票・docs のいずれからも名前で引かれない):
`app_band_pde_tdd.md` / `boot_splash_native_tdd.md` / `device_reservation_tdd.md` /
`gui_review_20260910_tdd.md` / `gui_review3_20260910_tdd.md` / `gui_review4_20260910_tdd.md` /
`highram_stage_tdd.md` / `memory_boot_tdd.md` / `paging_bounds_tdd.md` / `pgalloc_range_tdd.md` /
`physmem_synthetic_source_tdd.md`。
**削除の話ではない** — 対応する試験からリンクが張られていないだけで、証跡としては有効。

---

## 2. 分類

### (a) 重複 — 同じ契約を 2 か所以上で守っている (4 件)

| a | 重複 | 根拠 (見た行) | 残す案 |
|---|---|---|---|
| **a1** | `multiapp_model_host.c` (#17) と `multiapp_impl_host.c` (#18) | `test_multiapp_impl.py` の docstring: 「K5a's `multiapp_model_host.c` **hand-wrote** the state machine as a model. This harness compiles the **shipped kernel source** instead … and runs the **same numbered checks** against it」。実際にケース関数名を突き合わせると、model の 19 件のうち **17 件が impl に同名で存在**する (`case_fifth_refused` `case_exit_reclaims_one` `case_switch_only_in_op_wait` `case_no_preemption` `case_fault_isolated` `case_slot_bijection` `case_nomem_refuses` `case_cui_nesting` `case_abort_targets_running` `case_failed_start_is_clean` `case_gui_start_only_from_toplevel` `case_pick_rule` `case_park_decision` `case_no_starvation` `case_self_input_cannot_starve` `case_cross_round_bound`、および `case_poll_yield_is_lowest_priority` → `case_poll_yield`)。impl のみ 10 件。model のみ **2 件**: `case_launch_pending_parks` と `case_wait_key_joins_the_round` (impl は `case_wait_key`) | **impl を残す**。model 固有の 2 件を impl へ移してから model (1,438 行 / 62KB) を撤去する。設計文書としての価値は `multiapp_model_tdd.md` に残る |
| **a2** | `check-term-model` / `check-term-render` の `cargo test` と直後の `cargo check --lib` | `build/sdk.mk:110-111` と `115-116`。`cargo test` が同じクレートを既にビルドする。差は `cfg(test)` の有無だけ | `cargo check --lib` を落とす。ただし「test 無しでも lib が通る」を本当に見たいなら残す判断もある — **PM/ユーザー決裁** |
| **a3** | ページング系ハーネスの重複 (試験内容ではなく**仕掛け**) | `test_paging_bounds.py:12-18`、`test_app_band_pde.py:16-23`、`test_highram_stage.py:14-21`、`test_memory_boot.py:13-20` が、`kernel/paging.c` の**同じ 5 行の特権 asm 置換**を各自コピーしている | 試験そのものは別契約なので統合しない。置換だけを 1 つのヘルパへ (改修。今回はやらない) |
| **a4** | `kselftest.c` の `test_con_sink` / `test_kbd_inject` / `test_launch` / `test_tramp_user_str` / `test_db_v50` / `test_app_band_pde` と、対応するホスト試験 #51 #52 #53 #54 #59 #23 | `kernel/kselftest.c` のグループ名と、各ホスト試験の対象ファイルが一致 | **重複として扱わない**。ホスト試験は網羅 (106 / 51 / 115 / 20 / 409 / 107 表明)、kselftest 側は 7 / 5 / 3 / 3 / 6 / 1 表明の**煙試験**で、実機で毎起動踏むという別の価値がある。ただし 1 表明のものは (c) 参照 |

### (b) 不要 — 守っている契約がもう無い / 到達しない (3 件)

| b | 対象 | 根拠 (見た行) | 判断 |
|---|---|---|---|
| **b1** | `tools/tests/paging_rebuild_host.c` (39 `CHECK()`、5 モード) が `make check` から**到達しない** | `test_paging_bounds.py:6-8` が `--rebuild` を `choices=[nonmaster, rollback, sparse, attrs, final]` で受け、同 26 行 `harness = 'paging_rebuild_host.c' if args.rebuild else 'paging_bounds_host.c'`。`build/sdk.mk:132` は `python3 -B tools/tests/test_paging_bounds.py` と**引数なし**で呼ぶ | 「不要」ではなく**死んだ被覆**。`check-memory-host` に 5 モードを足すか、意図的に手動なら票に書く。**PM 決裁** |
| **b2** | `check-privileged` が構造上落ちない | `tools/check_privileged.py:145-152` — findings があっても `--strict` でなければ `return 0`。既定の根拠文は「**現在 userland は CPL=0 で動作**。リング3 導入時に `--strict` でゲートする」。しかし `CLAUDE.md` の Architecture 節は「External programs run at CPL=3 with their own page directory」と書いており、リング3 は既に入っている | 根拠文が現状と矛盾。`--strict` へ上げる / `make check` から外して手動に落とす / 現 offender を票に書いて例外表を持つ、のいずれか。**PM 決裁** |
| **b3** | 撤去済み機能の試験 — **見つからなかった** | T5b (常駐表示パネル) は `1c98613` で撤去され、PLAN.md 20〜24 行が「パネルが最初の押下を食う仕掛けに依存していたため削除した」と記録。`wm_tests.rs` に `panel` の語は 0 件。hermes の語は `tools/` `userland/` のコードに 0 件 (残るのは `docs/` 5 本 + `np21w_transport_tdd.md` の経緯記述のみ)。`t5a_display` も生きている (`build/programs.mk:450-451`、`build/app.conf:134` に `launcher` 宣言) | **撤去候補なし**。この観点での「不要」は現時点で存在しない |

### (c) 弱い — 構造ガードだけで振る舞いを見ていない (4 件)

| c | 対象 | 何が弱いか (見た行) | 振る舞いの試験にするには |
|---|---|---|---|
| **c1** | `tools/tests/test_gui_button_dispatch.py` (#38) | docstring が自認: 「**挙動試験ではない。** libos32gui にはホスト実行の足場が無いので、実ソースから `GUI_EV_BUTTON` の match アームを切り出して … 機械で確かめる」。中身は `arm.find("widget::on_button_down") > arm.find("MOUSE_BTN_LEFT")` という**文字位置の比較** (本文 63〜73 行)。`if` を `match` に書き換える / ガードを別関数に括り出す / 定数名を変える、のどれでも黙って通る (または黙って落ちる) | `libos32gui` には既に `host_tests/` クレートがある (#61)。`app.rs` の `on_event` を `#[path]` で取り込み、`GuiEvent { kind: GUI_EV_BUTTON, button: MOUSE_BTN_RIGHT }` を 1 つ流して「ウィジェットの `on_click` が呼ばれず `on_raw` は呼ばれた」を贋物で数える。2〜3 ケースで置き換えられる |
| **c2** | `tools/tests/test_filer_normalize.py` (#36) の 3 件目 `test_vfs_source_still_normalizes` | docstring は「両実装を**実ソースから抜き出して**ホストでビルドし、同じ入力で突き合わせる」と書くが、**ビルドして走るのは Rust 側だけ**。C 側は `fs/vfs.c` の `vfs_resolve_path` 以降 **2,600 バイトの窓**を切って `"== '.'"` / `"tmp[start+1] == '.'"` / `"if (tmp[p] == '/') { p++; continue; }"` の 3 文字列を `assertIn` するだけ (本文 92〜105 行)。空白 1 つ変えれば落ち、`.` の畳み方を変えても文字列が残れば通る | `vfs_resolve_path` は純関数なので、`test_vfs_mount_dev.py` と同じやり方で `fs/vfs.c` をそのまま `#include` し、Rust 側と**同じ 10 ケース**を C でも回して出力を突き合わせる。`vfs_mount_dev_host.c` に既に `fs/vfs.c` を取り込む足場がある |
| **c3** | `tools/tests/test_hsync_protect.py` (#58) | docstring: 「**実体規則 (`sys_stat` の inode 比較) は `hsync.c` 側にあり、ここではコンパイルが通ることだけを見る**」。72 表明はすべて字句正規化と名前規則に掛かっており、実際に `settings.db` を守る最後の砦 (inode 一致判定) は 1 表明も無い | `hsync.c` の inode 比較部分を `hsync_protect.inc` と同じく `.inc` に切り出し、`sys_stat` の贋物 (同 inode / 別 inode / stat 失敗) を 3 通り差し替えて「truncate に進むか進まないか」を見る |
| **c4** | `kernel/kselftest.c` の 1 表明グループ 4 本 — `test_kprintf` / `test_ring3_pd` / `test_map_user_keep` / `test_app_band_pde` | それぞれ `check()` 1 回。実質「その経路が呼べて落ちなかった」の確認で、境界も反例も踏んでいない。ファイル冒頭の方針 (「n=0 / バッファぴったり / 重なりコピー / 二重解放 **など、実際にバグが潜んでいた形だけ**を実機で毎回踏む」) に対して薄い | `test_app_band_pde` は #23 が 107 表明でホストを覆っているので、実機側は「境界の 1 点だけ」で妥当とも言える。方針を決めて票に書けば済む — **削除でも増強でもなく、意図の明文化**が要る |

### (d) 守るべきなのに試験が無い契約 (目についたもの。網羅ではない。6 件)

| d | 契約 | 現状 | 根拠 |
|---|---|---|---|
| **d1** | `hsync` が実体 (inode) で `settings.db` を守る — 契約 D0 の最後の砦 | ホスト試験なし (c3 と同じ穴) | `test_hsync_protect.py` docstring 自身が「ここではコンパイルが通ることだけを見る」 |
| **d2** | `st_dev` / `st_ino` をゲストで観測する | **観測手段が無い** (`stat` コマンド不在)。`vfs_mount_dev_host.c` の `stat_dev_identifies_mount` / `stat_dev_on_synth_root` 2 ケースがホストだけの根拠 | PLAN.md 28〜30 行が「`2005d70` の実機観測は未実施でホスト試験が根拠」と記録 |
| **d3** | G9 — 2 本以上のアプリを同時に ready にしたときの pick | **実機観測の手段が無く未実施**。ホスト模型 (#17/#18 の `case_pick_rule` / `case_no_starvation` 等) を受入とした | PLAN.md 9〜10 行「G9 は 2 本以上を同時 ready にする観測手段が無く未実施 … G9 はホスト模型を受入とし K5b は完了」 |
| **d4** | `libos32gui` のウィジェット描画と WM 配送 | ホスト試験は `cfgro` wrapper (35 件) だけ。描画・ジャンプ表・WM は「動かさない」と Cargo.toml が明記。Button 配送は構造ガード 1 本 (c1) | `userland/rust/libos32gui/host_tests/Cargo.toml` のコメント、`test_gui_button_dispatch.py` docstring |
| **d5** | ネットワーク L0〜L3 / M2〜M4 の回帰 | `make check` にも CI にも無く、`host_agent.py` の起動 + LINKTEST カーネルの配備 + 実機が要る。合否は各スクリプトの `RESULT:` 行 | `build/kernel.mk:186-187`「make check には入れない (実機が要る)」 |
| **d6** | T5b 撤去で消えた 4 本の書き直し | PLAN.md 20〜24 行は「**残件**: パネルに依存しない形で書き直す (W レーン、小)」としているが、`wm_tests.rs` には既に `taskbar_press_release_then_app_press_is_delivered` / `menu_…` / `modal_…` / `fep_press_release_then_app_press_is_delivered` の **4 本が揃っている** (加えて `button_up_on_taskbar_still_reaches_the_app_that_got_the_press`、`captured_release_is_delivered_while_modal_is_open` 等) | **PLAN.md の残件記述が古い可能性が高い**。着地時期は追っていない (**未読**) ので、PM が確認して PLAN.md を閉じるのが先 |

---

## 3. 推奨 (PM / ユーザーが決める前提の案)

### 3-1. 削除候補 (1 件)

| 案 | 対象 | 根拠 | 前提 |
|---|---|---|---|
| **R1** | `tools/tests/multiapp_model_host.c` (1,438 行 / 62KB) と `check-multiapp-model-host` からの `test_multiapp_model.py` | (a1) の通り、19 ケース中 17 ケースが `multiapp_impl_host.c` に**同名で**存在し、impl は出荷する `exec/appslot.c` をそのまま走らせる (`test_multiapp_impl.py` docstring)。手書き模型は K5a の設計凍結という役目を終えている | 先に model 固有の `case_launch_pending_parks` / `case_wait_key_joins_the_round` を impl へ移す。設計の記録は `multiapp_model_tdd.md` に残るので消えない |

**削除を勧めないもの**: T5b / hermes 由来の残骸は**見つからなかった** (b3)。`t5a_display` も
`libos32term` / `libos32term_render` も生きた依存なので撤去対象ではない
(`userland/rust/t5a_display/Cargo.toml:12-13`)。

### 3-2. 統合候補 (2 件)

| 案 | 対象 | 根拠 |
|---|---|---|
| **R2** | `check-term-model` / `check-term-render` の `cargo check --lib` を落とす | `build/sdk.mk:110-111` / `115-116`。直前の `cargo test` が同じクレートをビルドする (a2) |
| **R3** | ページング系 4 本の特権 asm 置換を 1 つのヘルパへ | `test_paging_bounds.py:12-18` ほか 3 本が同じ 5 行を複製 (a3)。**試験は統合しない** — 守る契約が別々 |

### 3-3. `make check` から外して手動に落とす候補 (2 件)

| 案 | 対象 | 根拠 |
|---|---|---|
| **R4** | SQLite の amalgamation を毎回コンパイルする 4 本 (`check-cfg-host` / `check-db-v50-host` / `check-install-recover-host` / `check-sqlite-groups-host`) を `make check-slow` のような別ターゲットへ分け、`make check` は残りだけにする | `lib/sqlite3/sqlite3.c` は **267,757 行**。4 ターゲットがそれぞれ独立にコンパイルする (各 `test_*.py` が `sqlite3.c` / `sqlite3.o` を自分の temp dir に作る)。**実行時間は測っていない**ので、分割の是非は PM が 1 回計測してから決めるべき。ここで言えるのは「4 回コンパイルしている」という事実だけ |
| **R5** | NP21/W ホスト道具の 4 本 (`test_np21w_ini` 49 / `test_np21w_ini_live` 47 / `test_np21w_trial` 31 / `test_np21w_transport` 8 = **135 件**) を `check-emu-tools` として分離 | `check-tools-host` は OS32 の契約 (filer / GUI / emu_agent / mk_settings_db) と、NP21/W の ini 道具 ([D2] の PM 専用道具) を同じターゲットに混ぜている。ソースを変えない日でも 135 件が毎回走る。**分離の是非も計測次第** |

### 3-4. CI に足すべきもの (2 件)

| 案 | 対象 | 根拠 | 注記 |
|---|---|---|---|
| **R6** | rustc **不要**の C / Python ホスト試験を CI へ。具体的には `check-con-sink-host` `check-kbd-inject-host` `check-launch-host` `check-ring3-str-host` `check-sh-launch-host` `check-sh-shell-host` `check-vfs-fd-sqlite-host` `check-vfs-mount-dev-host` `check-db-owned-host` `check-settings-protect-host` | 現在 CI は 6 ステップだけで、カーネルの契約 (con_sink / kbd_inject / launch / ring3_str / FD リース / mount エンコード) は**どれも CI で走っていない**。これらは `gcc` と `python3` だけで動く (各 `test_*.py` の FLAGS は `-m32` のホスト gcc) | **ただし全部が末尾で `i386-elf-gcc` のクロスコンパイル確認をする** (`test_con_sink.py` 等の docstring 「同じソースが `i386-elf-gcc -Werror` でも通ることを別に見る」)。CI にクロスコンパイラが無いので、**そのステップだけ切り離せる形にする改修が要る**。`check-settings-protect-host` の `test_hsync_protect.py` も同様 |
| **R7** | `check-gshell-host` (94 件) / `check-gui-host` (35 件) / `check-t5a-host` (62 件) / `check-term-*` (86 件) を CI に足すには **rustc が要る** | PLAN.md 12〜16 行が既に指摘済み: 「`check-gshell-host` や filer のホスト試験は**走っていない** (CI 環境に rustc が無い)。**CI 成功を GUI の動作確認とは扱わない。**」 | `actions/setup-rust` 系を 1 ステップ足せば届く範囲。`test_filer_normalize.py` は `rustup toolchain list` の**先頭**を使う (本文 68〜70 行) ので、CI では toolchain が 1 本であること |

### 3-5. 被覆を足す候補 (3 件)

| 案 | 対象 | 根拠 |
|---|---|---|
| **R8** | `paging_rebuild_host.c` の 5 モードを `check-memory-host` に足す (または「手動専用」と票に書く) | (b1)。39 表明が現在どの自動ゲートからも到達しない |
| **R9** | `test_gui_button_dispatch.py` を `libos32gui/host_tests` の贋物試験へ移す | (c1)。足場は既に `host_tests/src/fake.rs` にある |
| **R10** | `hsync` の inode 比較を `.inc` に切り出してホスト試験へ | (c3) / (d1)。契約 D0 の最後の砦が現在ノーガード |

### 3-6. 文書の整理 (2 件)

| 案 | 対象 | 根拠 |
|---|---|---|
| **R11** | PLAN.md 20〜24 行の「残件: パネルに依存しない形で書き直す」を確認して閉じる | (d6)。`wm_tests.rs` に 4 本が既にある。**着地時期は追っていない (未読)** |
| **R12** | 参照されていない `*_tdd.md` 10 本を対応する試験の docstring から 1 行リンクする | (1-E)。削除ではない。`con_sink_tdd.md` / `t9_tdd.md` / `s0_tdd.md` は既に試験側から引かれていて、その形に揃えるだけ |

---

## 4. 未読 / 調べていないこと

- **`apps/` と `game/`** — サブモジュールでこの worktree では空。指示にあった「`apps/` の filer の
  ホスト試験」は**読めていない**。in-tree の `userland/rust/filer/host/model_tests.rs` (3 件) は読んだ。
- **実行時間** — 1 本も実行していないので全て「未計測」。R4 / R5 の判断には PM の実測が要る。
- **`test_deploy_protect.py` の 164 件の中身** — クラス名だけ読んだ。`Base` / `ProtectJudgement` /
  `NhdSync` / `SyncFromHostdrv` / `NhdCli` / `HostdrvSync` / `Prune` / `Stamp` の 8 クラスに対して
  `ReviewB1`〜`ReviewB9` / `Review2`〜`Review5` と **6 回のレビュー往復ぶんの層**が重なっている
  (35 クラス)。同じ配備 4 系統を何度も踏んでいる可能性があるが、**本文を読んでいないので重複とは
  断定しない**。持ち主 (S0-D の担当) が読んで判断すべき最有力候補。
- **`cfg_host.c` (3,778 行 / 1,173 表明) の中身** — ケース名リスト (`test_cfg.py` の `CASES` 53 件) は
  読んだが、C の本文は読んでいない。この表で最大の試験なので、重複の有無は別途。
- **PLAN.md 20〜24 行の残件が実際にいつ閉じたか** — git 履歴は追っていない。
- **`docs/tasks/**/TASK_*.md` の受入条件の全件突き合わせ** — (d) は「目についたもの」だけで、
  票の受入条件を端から照合してはいない。
