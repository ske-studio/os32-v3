# Windows 作業ツリー (`C:\WATCOM\src\os32`) 未コミット作業の移植計画

> 発行: PM (2026-08-07) / 状態: **受入完了 (2026-08-07)**

*策定: 2026-08-07*

> 移植元: `/mnt/c/WATCOM/src/os32` — ブランチ `feat/vdm` @ `436ea5b` + **未コミット変更 86 件**
> (変更 66 ファイル / 新規 20、作業日 2026-06-05〜06-07)
> 移植先: 本リポジトリ `main` (KAPI v34, feat/vdm の8フェーズ選択取り込み完了済み)

---

## 0. 要約

Windows 側ツリーには、feat/vdm の**コミット後**に行われた作業が未コミットのまま残っている。
内容は大きく4系統:

| 系統 | 規模 | 性質 |
|------|------|------|
| **エンジンlib 修正** (board/battle/econ/inv) | 4ファイル / 約 +130 行 | 既存コードの修正。**本リポジトリの既知バグを直す** |
| **新規lib 3本** (rpg/save/turn) + テスト3本 + `rpg.db` | 21ファイル / 約 2,470 行 | 純追加。既存への影響ゼロ |
| **IME/FEP 拡張** (カーネル) | 6ファイル / 約 +900 行 + KAPI 6関数 | カーネル改変。自動テスト無し |
| **ツール/データ/文書** | 約 15ファイル | 検証基盤・ゲーム実データ・設計書 |

**移植順の原則**: 「実装完了 × 検証手段あり × 既存への影響が小さい」順に並べる。
具体的には ①検証基盤 → ②自動テストで即座に真偽が判る既存lib修正 → ③純追加の新規lib →
④テスト期待値と衝突するデータ → ⑤自動テスト不能なカーネル改変 → ⑥文書。

### 0.1 「動作確認済み」の判定根拠と限界

移植元ツリーで**テストが通ったという記録は残っていない** (`docs/logs/` に該当ログ無し、
`ENGINE_EXTENSION_PLAN.md` の完了条件チェックボックスは全て未チェック)。
判定に使えた客観的証拠は以下のみ:

- `.o` / `.bin` が全て生成済み (`rpg_test.bin` / `save_test.bin` / `turn_test.bin` 等、2026-06-07 23:04)
  → **コンパイル・リンクは通っている**
- `build/libs.mk` / `build/programs.mk` / `build/app.conf` / `tools/deploy.yaml` に配線済み
  → **ビルド・デプロイ経路まで作り込まれている**
- テスト3本に `check()` 群が実装済み (turn 7 / rpg 12 / save 5 グループ、計 791 行)
  → **検証コードは書かれている**

したがって本計画では **「実装完了」は確認済み・「動作確認」は本リポジトリ側で全てやり直す**。
各フェーズの完了条件に実機テスト結果を必ず含める。

### 0.2 移植元と本リポジトリの差分状況 (重要)

移植対象ファイルの `main` 現在値と Windows 側 HEAD (`436ea5b`) を md5 で全数照合した結果、
**エンジンlib・テスト・カーネルIME・アセットは全てバイト一致**。
よって Windows 側の未コミット差分は **そのままパッチとして適用できる**。

例外 (手マージが要るもの):

| ファイル | 理由 |
|----------|------|
| `tools/os32_server.py` | main 側も独自に変更済み。ハンク単位で適用 |
| `tools/nhd_deploy.py` | main 側で Phase G の `write-kernel` 削除済み。該当1行のみ適用 |
| `build/config.mk` / `build/kernel.mk` / `build/programs.mk` / `build/libs.mk` / `build/app.conf` | main 側が Phase E/G/H で先行変更済み。追加行のみ適用 |
| `tools/kapi.json` / `include/os32_kapi_shared.h` | **版番号系が違う** (下記 §0.3) |
| `docs/*.md` (6本) | main 側の 939497b 以降の更新を尊重。取らない |

### 0.3 KAPI 版番号の読み替え

Windows 側は feat/vdm 系 (v39 → v41)。本リポジトリは main 系 (現在 **v34**)。
差分の実体は **IME 関数6本の末尾追加のみ**。

| 移植元 | 本リポジトリ |
|--------|-------------|
| v39 → v41 (`ime_switch_dict` / `ime_user_list` / `ime_user_delete` / `ime_user_export` / `ime_user_clear` / `ime_trygetkey`) | v34 → **v35** (同6関数を末尾追加) |

`build/app.conf` の `apps/game 41` / `cmds/ime 40` も本リポジトリの版に読み替える
(`apps/game` は現在 34、`cmds/ime` は v35 で追加するので 35)。

### 0.4 移植しないもの

| 対象 | 理由 |
|------|------|
| `kernel/v86.c` / `v86.h` / `v86_debug.c` の差分 | V86 サブシステム自体を main に持ち込んでいない |
| `include/memmap.h` の `KHEAP_SIZE` 320KB→200KB | 移植元は MEMSYS5 300KB との帳尻合わせ。本リポジトリはメモリに余裕があり (SHM 終端 0x1C3000、0x200000 まで 244KB) 縮小の必要が無い。**フェーズ1で実測して判断する** |
| `programs/libos32db/libos32db.c` の `DB_SHM_PTR` 変更 | 本リポジトリは KAPI v34 `shm_base` で既に解決済み (レイアウト非依存でこちらが優れる) |
| `lib/sqlite3/os32_sqlite_vfs.c` の 300KB 化 | 本リポジトリは 400KB。フェーズ1後に再検討 |
| `docs/tasks/_archived/` 配下 24 ファイルの削除 | 履歴資産。main 側で維持 |
| `build/out/vmkernel.lz4` (バイナリ) | ビルド生成物 |

---

## フェーズ 1 — 検証基盤の整備 (最優先・低リスク)

以降の全フェーズの検証がこれに依存するため最初に行う。

**内容**
- `tools/os32_server.py`: 英数字26 + 数字10キーの `KEY_MAP` 自動追加、`SHIFT_SPACE`
  (IME ON/OFF) 同時押し送出、`SetForegroundWindow` によるウィンドウアクティブ化
  → **HTTP API からのキー注入が実用になる**。IME 検証 (フェーズ6) の必須前提
- `tools/nhd_deploy.py`: `subprocess` に `encoding='cp932', errors='replace'`
  → 日本語 Windows のコマンド出力が UTF-8 デコード失敗する問題の修正
  (本セッションで実際に踏んだ「did not contain valid UTF-8」の原因)

**適用方法**: `os32_server.py` はハンク単位で手適用 (main 側も変更済み)。`nhd_deploy.py` は1行。

**完了条件**: `curl :8032/key -d 'A'` で NP21/W にキーが届く。`make deploy-kernel` が
日本語出力を含んでも落ちない。

---

## フェーズ 2 — エンジンlib の DB 接続即時解放 + inv の RAM キャッシュ化

**本リポジトリの既知の不具合を直す、最も価値の高い移植。**

**背景**: 本セッションで `econ_init failed: -2` → `inv_init failed: -2` を踏み、
MEMSYS5 プールを 200KB → 400KB に拡張して回避した (`b4de0f6`)。
移植元はこれを**根本原因側で解決**している — 各エンジンlibは起動時に DB を
RAM キャッシュへ読み切るのに、**接続を開きっぱなしにしていた**。閉じれば同時接続は常に1本で済む。

**内容**

| ファイル | 変更 |
|----------|------|
| `programs/libos32board/board_core.c` | `board_init` 末尾で `db_close` (+2) |
| `programs/libos32battle/btl_core.c` | `btl_init` 末尾で `db_close` (+4) |
| `programs/libos32econ/econ_core.c` | `econ_init` 末尾で `db_close`、`load_items` の失敗を `kprintf` で顕在化 (+12/-1) |
| `programs/libos32inv/inv_core.c` | `shop_lineup` / `lottery_tables` を起動時 RAM ロード、`inv_init` 末尾で `db_close` (+56) |
| `programs/libos32inv/libos32inv.h` | `InvShopLineup` / `InvLotteryEntry` 型と上限定数 (+23) |
| `programs/libos32inv/inv_lottery.c` | 実行時 SQL 手組みを撤去し RAM 参照へ (+15/-47) |
| `programs/libos32inv/inv_shop.c` | 同上 (+14/-47) |

副産物として `inv_lottery.c` / `inv_shop.c` の**手書き SQL 文字列組み立て**(整数を1桁ずつ
`char` 配列に積む 40 行超) が消え、実行時 DB アクセスがゼロになる。

**注意**: 既存の `assets/items.db` に `shop_lineup` / `lottery_tables` が存在することは確認済み。
`DB_LOAD_TABLE_OPT` (テーブル不在を許容) も `libos32db_util.h` に既存。

**完了条件**
1. `inv_test` 120/120、`econ_test` 89/89、`btl_test` 83/83、`db_test` 9/9、`ai_test` 54/54 が維持
2. `game` が 4 DB 同時オープンを経ずにメインループへ到達
3. **MEMSYS5 プール実測**: 400KB → 200KB へ戻して上記が全て通るか確認。通れば
   `os32_sqlite_vfs.c` を 200KB に戻す (`b4de0f6` の実質的な巻き戻し)。
   通らなければ 300KB (移植元の値) で妥協し、理由を記録

---

## フェーズ 3 — 変身解除時の HP 比率維持 (`btl_transform`)

**内容**: `btl_transform_release` — `max_hp` を戻す際、現在 HP を単純クランプではなく
**比率維持で縮小**する (+9/-1)。負値クランプも追加。

**分離する理由**: フェーズ2 が「性能・資源の修正」なのに対し、これは**ゲーム挙動の変更**。
`btl_test` の期待値と衝突する可能性があるため独立コミットにして切り分け可能にする。

**完了条件**: `btl_test` 83/83。落ちた場合はテスト側の期待値が旧仕様である可能性を
`docs/06_battle_calc_spec.md` と突き合わせて判断し、記録を残す。

---

## フェーズ 4 — 新規lib 3本 (libos32turn / libos32rpg / libos32save)

**純追加。既存コードへの影響ゼロ**。設計書 `ENGINE_EXTENSION_PLAN.md` のステージA (M0) 相当。

**内容**

| lib | 規模 | 役割 | テスト |
|-----|------|------|--------|
| `libos32turn` | 387 行 (2 .c + .h) | 多人数手番回し / 週境界 (7ターン) / 最大ターン / 死亡者スキップ。DB不要 | `turn_test.c` 221 行 / 7 グループ |
| `libos32rpg` | 805 行 (5 .c + .h) | EXP曲線・レベルアップ成長・ステ配分・フィールド状態異常tick・死亡/リボーン・順位。`rpg.db` 駆動 | `rpg_test.c` 307 行 / 12 グループ |
| `libos32save` | 488 行 (2 .c + .h) | magic/version/CRC32 付きセーブ領域管理 (領域登録→スロット書込) | `save_test.c` 263 行 / 5 グループ |

**同時に持ち込むもの**
- `tools/gen_rpg_db.py` (138 行) → `assets/rpg.db`
- `build/libs.mk`: `DEFINE_LIB` 3行 + clean 1行 (main 側にマクロは既存)
- `build/programs.mk`: `DEFINE_TEST` 3行、`C_TESTS` の filter-out 3件、`programs` ターゲット、`.PHONY`
- `build/config.mk`: `PROGRAM_FLAGS` に `-Iprograms/libos32{turn,rpg,save}`
- `tools/deploy.yaml`: テスト3本 + `assets/rpg.db` → `/db/rpg.db`

**完了条件**: `make programs` 完走、実機で `turn_test` / `rpg_test` / `save_test` が全項目 PASS。
`rpg.db` が `/db/` に配置され `rpg_init` が成功。**落ちた項目は原因を切り分けて記録**
(移植元でも未検証のため、ここで初めて真偽が判明する)。

---

## フェーズ 5 — 実ゲームマップ (board.db 170マス) ⚠ 判断が必要

**内容**
- `tools/gen_board_db.py` を全面書き換え (+146/-69): テスト用12マス →
  **オノコロ島10マス + 全8ステージ×20マス = 170マス / 178接続 / 9区画**、マス種別13種
- `assets/board.db` を再生成
- `programs/libos32board/libos32board.h`: `BOARD_MAX_AREAS` 8 → **16** (9区画に必要)

**⚠ 未解決の衝突**: 現行 `board_test.c` は**旧テストデータ (マス 0-11 / 20-25 の固定トポロジ)
を前提に書かれている**。board.db を実マップに差し替えると board_test は成立しない。
移植元は board_test を更新していない (両ツリーでバイト一致)。

> 補足: 本リポジトリの `board_test` は現状 **34/69 失敗** している。これは本移植とは無関係の
> 既存不具合 (旧データ・旧テストの組で失敗している) であり、本フェーズで一緒に解決するのが自然。

**推奨案 (A)**: テスト用と実ゲーム用の DB を分離する。
`gen_board_db.py` に `--test` / `--game` を設け、`assets/board_test.db` (旧12マス、
board_test 専用) と `assets/board.db` (実マップ、ゲーム用) を出し分ける。
board_test は `/db/board_test.db` を読むよう1行変更。
→ 既存テストを壊さずに実マップを導入できる。

**代替案 (B)**: board_test を実マップ前提に書き直す。テスト資産としては正しいが作業量が大きく、
現在の 34 失敗の切り分けと混ざるため非推奨。

**完了条件 (案A採用時)**: `board_test` の合否が移植前 (34/69失敗) から**悪化しない**こと。
`game` が実マップをロードし、盤面 170 マスで移動できること (スクリーンショット)。

---

## フェーズ 6 — IME/FEP 拡張 (KAPI v34 → v35)

**最大かつ最もリスクの高い移植。カーネル改変で自動テストが無い。**

**内容**

| ファイル | 変更 | 内容 |
|----------|------|------|
| `kernel/ime.c` | +372/-57 | 候補リストウィンドウ (↑↓/数字/ページング)、描画バックエンド抽象化、辞書管理API |
| `kernel/ime.h` | +25/-4 | `IME_ST_*` 状態定数、`state`/`page`/`per_page`/`render` フィールド追加 |
| `kernel/ime_dict.c` | +186/-0 | ユーザ辞書の一覧/削除/エクスポート/全消去、`ime_dict_reopen` |
| `kernel/ime_render.h` | 新規 29 行 | 描画プリミティブ関数ポインタ表 |
| `kernel/ime_render_tvram.c` | 新規 76 行 | TVRAM バックエンド実装 |
| `kernel/console.c` | +38/-18 | **TVRAM スクロール保護** — 下端 N 行を固定し FEP 候補窓が流れないようにする |
| `include/tvram.h` | +3 | `tvram_set_scroll_reserve()` |
| `programs/cmds/ime.c` | 新規 200 行 | 辞書管理シェルコマンド |
| `programs/apps/edit/sys_keyboard.c` | +1/-1 | `kbd_trygetkey` → `ime_trygetkey` (エディタで日本語入力可に) |
| `build/kernel.mk` | +1 | `kernel/ime_render_tvram.c` を C_KERNEL に追加 |
| `build/app.conf` | +1 | `cmds/ime 35 0` |
| `tools/kapi.json` + 生成物 | +6 関数 | **KAPI v35** |

**KAPI v35 の6関数** (末尾追加・append-only 厳守):
`ime_switch_dict(int)` / `ime_user_list(const char*, void*, int)` /
`ime_user_delete(const char*, const char*)` / `ime_user_export(const char*)` /
`ime_user_clear()` / `ime_trygetkey()`
→ 追加後に `python3 tools/gen_kapi.py && python3 tools/kapi_rust_gen.py`、
`git diff --exit-code` で SSOT 同期を確認。

**互換性の確認事項**
- `IME_MODE_*` は `include/os32_kapi_shared.h:323` に定義済みで、`ime.h` から消えても
  `ime.c` からは参照可能 (照合済み)。**追加作業不要**
- `IME_MODE_*` / `IME_ST_*` の参照は `kernel/ime.{c,h}` 内に閉じている (全数 grep 済み)

**完了条件**
1. `make kernel && make programs` 完走、KAPI 生成物が SSOT と一致
2. 起動回帰 (HDD ブート、`ver` が v35 を表示)
3. **フェーズ1のキー注入で検証**: `SHIFT_SPACE` で IME ON → ローマ字入力 → 変換 →
   候補リスト展開 (↑↓/数字/ページ送り) → 確定。各段階をスクリーンショットで確認
4. スクロール保護: 画面下端で入力しても候補窓が流れないこと
5. `ime` コマンドで辞書一覧/削除/エクスポートが動くこと
6. `edit` で日本語入力ができること

**リスク**: 手戻り時の切り分けを容易にするため、**カーネル側 (ime*/console/tvram/KAPI)** と
**ユーザ側 (`cmds/ime.c`, `edit`)** の2コミットに分ける。

---

## フェーズ 7 — 文書整備

**内容**
- `docs/tasks/game/GAME_PORT_PLAN.md` (409行) / `ENGINE_EXTENSION_PLAN.md` (416行) を取り込み。
  移植元は「ゲーム移植の親計画 + エンジン拡張別冊」で、フェーズ4/5 の設計根拠にあたる。
  取り込み時に**進捗チェックボックスを本リポジトリでの実績に合わせて更新**する
  (ステージA = フェーズ4、Phase 0/1 = 既存 `programs/apps/game/` + フェーズ5)
- `docs/tasks/fep/` 8本 (00_INDEX / 01_UI_CANDIDATE / 02_SCROLL_GUARD / 03_DICT_COMMAND /
  04_DICT_QUALITY / 05_GFX_MODE / 06_MISC / 07_BUNSETSU) を取り込み。
  フェーズ6 の設計書にあたる。実装済み (P1/P2/P3) と未実装 (P4/P5/P6/P7) を明示
- `docs/tasks/fep/FEP_FUTURE.md` の更新差分を適用
- **`docs/*.md` 本体6本 (06_filesystem / 08_build / BOOT_ARCHITECTURE / DEVELOPMENT /
  INDEX / POLICY_DEV) の移植元差分は取らない** — main 側が Phase A/G/H で先行して
  更新済みのため。代わりに **既存タスク #9 (ドキュメント同期)** の中で
  本移植の結果 (新規lib 3本・IME v35・実マップ) を反映する
- `docs/tasks/_archived/` の削除差分は適用しない (履歴資産を維持)

---

## 移植順まとめ

| # | フェーズ | 規模 | 検証手段 | リスク | 既存への影響 |
|---|---------|------|----------|--------|------------|
| 1 | 検証基盤 (os32_server / nhd_deploy) | 小 | 手動 curl | 極小 | ツールのみ |
| 2 | エンジンlib DB解放 + inv RAM化 | 中 | 既存テスト5本 | 小 | **既知バグを修正** |
| 3 | 変身解除 HP 比率 | 小 | `btl_test` | 小 | 挙動変更 |
| 4 | 新規lib 3本 + テスト + rpg.db | 大 (2,470行) | 新規テスト3本 | 小 | **ゼロ (純追加)** |
| 5 | 実マップ board.db | 中 | `board_test` + 実機 | 中 | ⚠ テスト衝突 (§フェーズ5) |
| 6 | IME/FEP + KAPI v35 | 大 (900行+KAPI) | キー注入 + 目視 | **大** | カーネル改変 |
| 7 | 文書 | 中 | — | 極小 | なし |

各フェーズ1コミット (フェーズ6 のみ2コミット)、常にビルドグリーンを維持する。

## KAPI 台帳 (main 番号系)

| フェーズ | 版 | 変更 |
|---------|-----|------|
| 現在 | v34 | `shm_base` (150関数) |
| 6 | **v35** | + `ime_switch_dict` / `ime_user_list` / `ime_user_delete` / `ime_user_export` / `ime_user_clear` / `ime_trygetkey` (末尾追加) |

## 未決事項

| # | 内容 | 推奨 |
|---|------|------|
| D1 | フェーズ5 の board.db 衝突 | **案A** (テスト用/ゲーム用 DB 分離) |
| D2 | MEMSYS5 プールを 400KB から戻すか | フェーズ2 で実測し 200KB を試す |
| D3 | `board_test` の既存 34 失敗を本移植と別で追うか | 別課題として切り出す |
| D4 | Windows 側ツリーの扱い | 移植完了後、Windows 側を `main` に追従させるか要相談 |

---

# 実施結果 (2026-08-07)

全7フェーズを実施し、8コミットで `main` に取り込んだ。各フェーズは実機
(NP21/W, HDD ブート) で検証済み。

| # | フェーズ | コミット | 結果 |
|---|---------|---------|------|
| 1 | 検証基盤 | `c1951bb` | `/key` でゲストにキーが届くことを確認 |
| 2 | DB接続解放 + inv RAM化 | `8d79e46` | 既存テスト5本を維持し、MEMSYS5 を 400KB → **200KB** に復帰 |
| 3 | 変身解除 HP 比率 | `d5d6795` `f69986c` | btl_test の期待値を更新して 83/83 |
| 4 | 新規lib 3本 | `a624627` | turn 35/35・rpg 60/60・save 15/15 |
| 5 | 実マップ board.db | `57ea447` | board_test **34/69 → 87/87**、game が170マスを描画 |
| 6 | IME/FEP + KAPI v35 | `3455160` `24fac50` | v35 起動、`ime` コマンド動作、プリエディット描画確認 |
| 7 | 設計文書 | (本コミット) | game 2本 / fep 8本を取り込み進捗を更新 |

## 計画との差異

- **フェーズ3の検証をやり直した。** 当初 `make deploy` (HostDrv) 後にテストを
  実行して 83/83 としたが、これは誤り。HostDrv はゲストの `/host` にしか
  配置せず、PATH 解決では NHD 側の `/usr/bin` が優先されるため、**古い .bin が
  黙って実行されていた**。NHD フルデプロイでは 82/83 で落ち、落ちていたのは
  変身解除の期待値そのものだった (テストが旧実装の不具合を固定していた)。
  以降のフェーズは全て NHD フルデプロイで検証している。

- **フェーズ5 の board_test 既存不具合の原因が判明した。** 34/69 の失敗は
  ライブラリのバグではなく、テストが `/host/assets/board.db` という存在しない
  パスを開いていたためだった (deploy.yaml は `assets/board.db` を `/db/` に置く)。
  参照先を `/db/board_test.db` に修正して 87/87 になった。§未決事項 D3 は解消。

- **フェーズ6 で `IME_UserEntry` / `IME_MODE_*` の移動が追加で必要だった。**
  移植元は両者を `kernel/ime.h` から `include/os32_kapi_shared.h` へ移していた
  (ユーザ空間の `ime` コマンドが参照するため)。当初の調査では移植元ツリー内で
  grep していたため main 側に既にあると誤認していた。

- **`programs/apps/edit` の FEP クラッシュが解消した。** 移植前は edit 内で
  FEP をトグルすると "Process crashed" + スタックトレースで落ちていた。
  移植後は落ちない。計画時には把握していなかった副次的な改善。

## 未決事項の帰結

| # | 内容 | 結果 |
|---|------|------|
| D1 | board.db の衝突 | **案A を採用**。`--game` / `--test` で board.db と board_test.db を出し分け |
| D2 | MEMSYS5 プールを戻すか | **200KB に復帰**。db_close 修正で同時接続が1本になり余裕が生まれた |
| D3 | board_test の既存34失敗 | **解消**。原因は不正なDBパス (上記) |
| D4 | Windows 側ツリーの扱い | **未決**。移植は完了したので、Windows 側を `main` に追従させるか要相談 |

## 積み残し

- **漢字変換が候補ゼロになる** (移植6)。`/db/fep.db` に該当エントリが存在するのに
  `ime_dict_search` が 0 を返す。完全一致・前方一致の双方で再現。本移植は
  変換パスを一切変更しておらず、MEMSYS5 を 400KB に戻しても再現するため
  **移植前から存在する不具合**。FEP の P4 以降に着手する前に解決が必要。
- **`docs/tasks/lib{turn,rpg,save}/` の設計書**は未配置
  (`ENGINE_EXTENSION_PLAN.md` §1〜§3 が実質の設計書として機能している)。
- **ゲームが ESC で終了しない** — WIP のため。
- `docs/tasks/game/` の各文書が参照する移植元 `sample/game/` は
  `.gitignore` で除外されており本リポジトリには含まれない。
