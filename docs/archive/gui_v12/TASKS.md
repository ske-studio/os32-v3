# GUI シェル v1.2 — 作業分担票

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**  
> 親: [../DESIGN.md](../../tasks/gui/DESIGN.md)  
> 基底契約: [../API_CONTRACTS.md](../../tasks/gui/API_CONTRACTS.md)  
> v1.2 追加契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md)  
> 前提: GUI v1.1 G1〜G5 完了

---

## 0. 方針

v1.2 は HAL 拡張版ではなく、**v1.1 で完成した GUI 基盤をデスクトップ環境として完成させる版**とする。

PEGC / Cirrus / 9801 backend は原則 freeze し、H lane は regression 検証のみ。

**KAPI v42 は freeze。v43 は network / Host Services 用の予約で GUI は使わない。**

新機能は既存 `gui_call` と `libos32gui` 上に実装する。

v1.1 で問題になった「X4 で重い処理」「暗黙 ABI 変更」「共有 PT」「起動途中状態」のような跨層変更を避け、v1.2 は W / C 中心に進める。

---

## 1. レーンと票

| レーン | 言語 / 場所 | 票 | 内容 |
|---|---|---|---|
| **K** protocol | C / Rust, `sdk/include/os32/`, `sdk/rust/os32api/src/gui/` | [TASK_K5_v12_proto.md](TASK_K5_v12_proto.md) | op 65/66、Modal result、Session request、Quit reason。KAPI追加なし |
| **W** WM | Rust, `userland/gshell/` | [TASK_W3_desktop_session.md](TASK_W3_desktop_session.md) | taskbar / Start / clock / launcher / session state machine / halt |
| | | [TASK_W4_dialogs.md](TASK_W4_dialogs.md) | message / file-open / input dialog、completed result、sticky Modal |
| **C** library | Rust, `userland/rust/libos32gui/` | [TASK_C4_desktop_api.md](TASK_C4_desktop_api.md) | modal/session API、Icon16、shlib entry 95〜100 |
| **C** app | Rust, `userland/rust/filer/` | [TASK_C5_filer.md](TASK_C5_filer.md) | File Manager |
| **H** HAL | C, `gfx/`, `drivers/` | 新規票なし | 9801 / PEGC / Cirrus regression のみ |
| **PM / 検証** | Python / NP21/W | 本書 §5 | 既存 `/api/mouse` を使う自動 gate、契約照合、ROADMAP 更新 |

### レーン原則

- H lane は原則変更禁止。regression で backend bug が見つかった時だけ bug ticket を別に切る。
- `sdk/kapi.json` は v1.2 では変更しない。
- GUI op/event/struct、shlib jump table は末尾追記のみ。
- AppVTable の既存 field 配置は変更しない。
- popup Window ABI は v1.2 では作らない。

---

## 2. 依存と順序

```text
                  ┌────────► W3 ────────────────┐
G0 contract ─► K5 ┤                              ├──► G4 / G5
                  ├────────► W4 ──┐             │
                  └────────► C4 ──┼──► C5 ──────┘
                                  │
existing /api/mouse + gates ──────┘
```

### 並列開始可能

K5 が protocol 数値を commit した後、次の 3 本を並列化できる。

- W3
- W4
- C4

C4 は W4 implementation 完了を待たず wrapper を作れるが、結合 gate G2 は W4 後。

### C5 開始条件

- C4 の公開 API / shlib entry が固定済み。
- W3 `SESSION_REQUEST` が動作。
- W4 modal result が動作。

---

## 3. 衝突ゾーンの所有権

| 所有 | ファイル |
|---|---|
| K5 | `sdk/include/os32/os32_gui_shared.h`、`sdk/rust/os32api/src/gui/proto.rs` |
| W3/W4 | `userland/gshell/**`。W3/W4 間の同一 file は PM が順序を決める |
| C4 | `userland/rust/libos32gui/**`、`sdk/rust/os32api/src/gui/stub.rs` |
| C5 | `userland/rust/filer/**` |
| PM | `docs/tasks/gui/v12/**`、`tools/**`、`docs/ROADMAP.md` |
| H | `gfx/**`、`drivers/**` — freeze |

共有 library jump table は C4 だけが変更する。

既存 entry 0〜94 は変更禁止。v1.2 公開 entry は 95〜100。

`tools/check_gui_proto.py` は PM 所有。K5 の定数追加に合わせて PM が照合項目を更新する。

共有 build/deploy file (`build/programs.mk`, `userland/deploy.yaml`) は PM 経由。

---

## 4. 各票の要約

### K5 — protocol completion

固定:

```text
GUI_OP_MODAL_RESULT      = 65
GUI_OP_SESSION_REQUEST   = 66
GUI_MODAL_INPUT          = 4
GUI_SESSION_LAUNCH       = 1
GUI_SESSION_SWITCH_CUI   = 2
GUI_SESSION_SHUTDOWN     = 3
GUI_QUIT_REASON_REPLACE_APP = 1
GUI_QUIT_REASON_SWITCH_CUI  = 2
GUI_QUIT_REASON_SHUTDOWN    = 3
```

加える struct:

- `GuiReqModalResult` 4B
- `GuiRespModalResult` 260B
- `GuiReqSession` 260B

`GUI_PROTO_VERSION=1` / KAPI v42 を維持。

詳細: [TASK_K5_v12_proto.md](TASK_K5_v12_proto.md)

### W3 — desktop / session

- taskbar 24px
- Start / Programs(max 96) / File Manager / Run / CUI / Shut Down
- work area
- clock dirty rect
- SessionAction 1 件
- external app から session request
- sticky Quit
- nested `exec_run()` 禁止
- CUI cfg 更新失敗時の rollback
- `for (;;) sys_halt()` の terminal halt

詳細: [TASK_W3_desktop_session.md](TASK_W3_desktop_session.md)

### W4 — dialogs

- completed modal result / slot
- result 未 consume 中の next modal = `ERR_FULL`
- wrong/double consume = `ERR_STALE`
- Modal event sticky delivery
- File Open path result
- Input dialog + FEP
- owner cleanup

詳細: [TASK_W4_dialogs.md](TASK_W4_dialogs.md)

### C4 — client API

shlib:

```text
95  modal_open
96  modal_result
97  file_open
98  input_open
99  session_request
100 draw_icon16
```

`SHLIB_NFUNC=101`。

AppVTable は変更しない。File/Input は非同期。

Icon16 は 160B (`4bpp pixels 128B + 1bpp mask 32B`)。

詳細: [TASK_C4_desktop_api.md](TASK_C4_desktop_api.md)

### C5 — File Manager

- 2 pane
- lazy directory tree
- navigate / mkdir / rename / delete / rmdir
- file copy / same-FS move
- `.bin` launch は `SESSION_REQUEST`、直接 exec 禁止
- right-click menu は client surface overlay
- large copy は最大 16KB / event-loop cycle 程度に分割

KAPI v42 の既存 `sys_ls/stat/mkdir/rename/unlink/rmdir/open/read/write/close` だけを使う。

詳細: [TASK_C5_filer.md](TASK_C5_filer.md)

---

## 5. 検証層

v1.1 完了後に NP21/W ai-debug の **`/api/mouse` は実装済み**。v1.2 では新設作業ではなく、この API を gate automation に利用する。

最低操作:

- absolute move
- left down / up
- right down / up

併用:

- `/api/key`
- `/api/screenshot`
- `/api/status`

PM / 検証層は以下を維持・拡張する。

- `tools/check_gui_proto.py`
- `make check-shlib`
- `make check`
- GitHub Actions static gate
- `make external` が必要な SDK / shlib 更新の検出
- 9801 / PEGC / Cirrus 3 backend regression

### PM の成果物 (票なし、ここがチェックリスト)

| 成果物 | 時期 | 内容 |
|---|---|---|
| `tools/check_gui_proto.py` | **G0 の前** (v1.1 で未作成だった) | `os32_gui_shared.h` ⇄ `proto.rs` の定数 (op / event / modal / session / quit reason / flags / 上限) と構造体の大きさ・フィールド並びを照合。`make check` と GitHub Actions に載せる |
| `tools/gui_gate.py` | G1 まで | `/api/mouse` (`ax/ay` = シームレス絶対座標) + `/api/key` + `/api/screenshot` + `/api/status` で G1〜G5 の操作列を回す。v1.1 のドラッグ / 重なり回帰も含む |
| 配備登録 | C5 / C4 のテストアプリ着手時 | `build/programs.mk` (`DEFINE_RUST_PROGRAM`)、`userland/deploy.yaml`、`build/app.conf` |
| 契約照合 | K5 完了時 | C / Rust の値・size・offset、KAPI v42 / 180 関数のまま |
| ROADMAP / TASKS §10 | ゲート通過ごと | 記録 |

### 自動化で必ず mouse を使う項目

- Start button
- taskbar window button
- right-click context menu
- dialog button
- File Manager selection
- v1.1 drag/overlap regression

---

## 6. ゲート

| Gate | 名前 | 通過条件 |
|---|---|---|
| **G0** | v1.2 contract freeze | K5 値 / size / offset 固定、KAPI v42、v43不使用、個別5票存在 |
| **G1** | Desktop | taskbar / Start / clock / focus / Programs を `/api/mouse` で自動操作。clock partial present |
| **G2** | Dialog/API | message/file/input、path/text返却、FEP、sticky Modal、ERR_FULL/STALE、old/new shlib組合せ |
| **G3** | Filer | navigate / mkdir / rename / copy / move / delete / context menu / error path |
| **G4** | Session | app→別app、app→CUI、halt、CTRL+STOP後 pending action、nested exec なし |
| **G5** | Release | pc98 / PEGC / Cirrus + v1.1 G1〜G5 regression + shlib mismatch + `make check` |

---

## 7. 主要シナリオ

### 7.1 app replacement

1. `gui_demo` 起動。
2. Start -> Programs -> File Manager。
3. SessionAction `LAUNCH(/usr/bin/filer.bin)`。
4. `gui_demo` へ sticky `Quit(REPLACE_APP)`。
5. gui_demo 終了。
6. 既存 `exec_run()` が gshell top-level へ戻る。
7. filer 起動。

**WM handler / X3 / X4 内の nested `exec_run()` は 0 回。**

### 7.2 CUI switch

1. filer 起動中。
2. Start -> CUI mode。
3. sticky Quit。
4. filer 終了。
5. cursor hide / FEP callback解除 / gfx shutdown。
6. cfg `GUI=0` 更新成功。
7. shell switch。
8. `ver` 応答。

cfg 書込みを失敗注入した場合は shell switch せず GUI に残る。

### 7.3 System halt

1. app 起動中。
2. Start -> Shut Down。
3. sticky Quit。
4. app 終了。
5. GFX/FEP cleanup。
6. halt screen。
7. `for (;;) sys_halt()`。

タイマ IRQ 等が入っても desktop へ復帰しない。

### 7.4 Unresponsive app

1. `gui_busy`。
2. SessionAction を予約できる文脈で予約。
3. Quit を無視 / loop 継続。
4. CTRL+STOP。
5. owner 回収。
6. pending SessionAction 続行。
7. gshell / kernel 生存。

### 7.5 Modal overflow

1. event ring を人工的に満杯近くまで埋める。
2. File/Input dialog 完了。
3. completed result 保存。
4. Modal event は pending。
5. app が poll して空き生成。
6. Modal event 再配送。
7. `MODAL_RESULT` で value 取得。

### 7.6 Large file copy

1. 十分大きい file を copy。
2. 1 cycle 最大 16KB 程度。
3. copy 中も cursor / clock / event loop が進む。
4. Quit を受けたら copy state/fd を閉じて exit 可能。

---

## 8. v1.2 でやらないもの

- GUI terminal
- CUI output redirect
- multi-process GUI
- popup-window ABI
- drag & drop
- recursive directory copy/delete
- cross-FS move
- thumbnail
- arbitrary icon formats
- new GFX backend
- >640x480
- command-line args / PATH search in Run
- true power-off

---

## 9. 完了定義

v1.2 完成とは、CUI command 入力なしで:

```text
OS boot
 -> GUI
 -> Start
 -> File Manager
 -> file operations
 -> GUI app launch
 -> another app launch
 -> CUI switch / system halt
```

を完走できること。

同じ flow が:

- PC-9801 planar 640x400
- PEGC 640x480
- Cirrus 640x480

で成立し、v1.1 regression がないこと。

---

## 10. 進捗記録

**現在の状態**: 2026-09-07 に main へマージ済み (`d739494`、親 `0b3286c` / `69ccb23`)。
以下の「レビュー待ち」「マージ待ち」は当時の記録。マージ確認はローカル Git の履歴に基づき、
今回の記録整合ではゲスト試験を再実行していない。9801 の v11 台本で Help の閉じるクリックが
座標ずれで外れる制約は、最終検証記録のとおり残る。

| 日付 | 記録 |
|---|---|
| 2026-09-06 | v1.2 設計採用。`CONTRACTS.md` / `TASKS.md` 初版を発行 |
| 2026-09-06 | 設計再点検を反映: app→gshell の `SESSION_REQUEST`、shutdown の非復帰 halt、sticky Quit/Modal、modal consume 規則、KAPI v42 freeze / v43 network予約、既存 `/api/mouse` 前提、個別 K5/W3/W4/C4/C5 票を追加 |
| 2026-09-06 | **PM 点検 (実装前、コードと突き合わせ)**。**一致を確認**: op 64 / 80、EV_MODAL 11 / EV_QUIT 12、modal kind 0〜3、`GuiString` 256B、`GuiReqModal` 260B (→ `GuiRespModalResult` / `GuiReqSession` 260B は整合)、shlib entry 95 本 (0〜94)、KAPI v42 に `sys_time` (`os_time_t`) / `sys_ls`〜`sys_close` / `sys_halt` / `sys_switch_shell` / `exec_run` あり、`OS32_ERR_FULL` -13 / `STALE` -11 あり、libos32gui の U3 ループは `GUI_EV_QUIT` で戻る (v1.1 アプリも Quit で終了できる)。**ずれ・不足**: (1) `OS32_ERR_AGAIN` は無い → C4 の modal_result は「event 到着後だけ呼ぶ API」で確定 (未完成 query は公開しない)。(2) **`tools/check_gui_proto.py` は v1.1 で未作成** (`make check` にも無い) → G0 の前に PM が作る (C ⇄ Rust の定数 / size / offset 照合、CI にも載せる)。(3) **gshell は右ボタンを一切扱っていない** (`input.rs` は左のエッジのみ) → W3 に「右ボタンのエッジ取り込み + `wm_owns_edge` の扱い + Button イベント配送」を明記する (D4 / F5 の前提)。(4) gshell は今 `GUI_EV_QUIT` を送っていない → W3 の sticky Quit は新設 (既存経路なし)。(5) 現行の ESC (「CUI へ」) は cfg を書かない即時切替。S6 の SWITCH_CUI は `GUI=0` を永続化する → **ESC を SessionAction 経由にするか (永続化)、開発用の一時切替として残すかは要決定**。(6) 現行の上部バー (`ESC:CUI F1..F5`) と v1.2 の下部 taskbar の関係が未記載 → v1.2 で撤去 / デバッグ用に残すを W3 に書く。(7) `OS32ShlibHeader.version` は 1 のまま (nfunc で拒否) と明記する。(8) Programs は `/usr/bin` の全 `.bin` (テスト用も含む ~40 本) を並べる → v1.2 はそれでよいが、将来のフィルタ (manifest / 拡張子以外の印) を対象外として記す。(9) PM 側の成果物 (`check_gui_proto.py`、`/api/mouse` を使う gate スクリプト、filer / v12_api_test の deploy 登録) に票が無い → 本書 §5 に PM チェックリストとして列挙する。(10) `make external` は apps/game が libos32gui を使わない限り不要 (SDK の libos32gfx を変えたときだけ) と §9 に注記。実装はユーザー指示待ち |
| 2026-09-06 | **ユーザー決定**: ESC 即時切替は完成後 (G5) に廃止。CUI へ戻るのは Start → 確認ダイアログ → SessionAction の 1 本だけ (無言の 1 キーで CUI に落ちると GUI の状態が失われて危険)。開発中はデバッグ用に残す。上部バーも同じ扱い。**着手前の提案はすべて承認** → 反映: CONTRACTS S6 / D2 / D4 / V12-C / §8 / §9、W3 §4.1〜4.2 (デバッグ残置と撤去、右ボタン)、C4 (AGAIN 無し、shlib version 1)、本書 §5 の PM チェックリスト。**`tools/check_gui_proto.py` を作成** (定数 96 件 / 構造体 28 件、packed と union 対応、C 側に `GUI_SLOT_MAX` を追加して Rust と揃えた)、`make check-gui-proto` と GitHub Actions に組み込み。G0 の前提が揃った。実装 (K5 →) は次の指示で開始 |
| 2026-09-06 | **実装開始** (ユーザー指示)。順序: K5 (Opus、worktree) → W4 + C4 並列 → W3 (確認ダイアログと Run... が W4 の Input を使うため W4 の後) → C5。PM は `tools/gui_gate.py` (v1.1 回帰の台本、以後 v1.2 の台本を足す) を先に用意 |
| 2026-09-06 | **K5 済み → G0 通過** (`2cdc6ed`、feat/gui `fed1f14`): op 65/66、`GUI_MODAL_INPUT 4`、session / quit reason、`GuiReqModalResult` 4B / `GuiRespModalResult` 260B / `GuiReqSession` 260B を C と Rust に末尾追記、size / offset の static assert、`check_gui_proto` 105 定数 / 31 構造体一致、KAPI v42 / PROTO 1 のまま。`gui_gate.py v11` は PEGC 480 で通過 (drop 後の座標を補正)。**W4 と C4 を Opus で並列投下** (worktree。`.env` が無いので `cp ~/os32/.env .` を指示) |
| 2026-09-06 | **W4 (`51361d1`) と C4 (`772e48e`) をマージ** (feat/gui `67fd713`)、`v12_api_test` を PM が登録 (`81b0469`)。**G2 の実機**: `v12_api_test` (F5 で `/usr/bin` から起動) で MessageBox → `modal_result` r=1、File Open → `/DISK.FDI`、キャンセル r=0、Input で FEP 変換「日本語abc」の往復、icon16 の透過、マウスでのボタン操作、`session_launch` は W3 前なので ERR_NOSYS (期待どおり)。WM の F5 ダイアログはディレクトリ降下 / パス行 / ページ送りが動く。**見つけて直した**: モーダルを開いて OP_WAIT で寝ているアプリに CTRL+STOP が効かなかった (abort 判定が syscall 入口と CPL=3 割り込みだけ) → syscall 出口でも判定 + gshell が raw の CTRL+STOP で待ちを抜ける (`748c053`、実機で `ring3_abort_count=1`、直後の WM ダイアログが FULL にならない)。**残り (G2)**: ERR_FULL / ERR_STALE / リング満杯 (§7.5) の否定試験 → C4 にテストキー 5/6/7 を追加依頼。`/api/key` の連打はキーが落ちるので 0.3s 間隔で送る。**W3 を投下** |
| 2026-09-06 | **W3 (`3a6bcee`) をマージ** (feat/gui `6617f03`)。hotdeploy で **G1 の目視**: taskbar (Start / 時計 20:24 → 20:25 の更新)、Start menu 5 項目、Programs 一覧 (/usr/bin、アルファベット順)、右クリックのデスクトップ menu。**注意**: C4 コーダーに「CUI で再現してよい」と許可したため同じエミュレータへ hotdeploy が並行し、ゲストの `v12_api_test.bin` (ヘッダの flags / entry が壊れて EIP 0x50C639) と `gui_demo.bin` (invalid) が私の検証と競合した → コーダー完了後にローカル AI で NHD 配備からやり直す。**以後、エミュレータは同時に 1 人**: コーダーには hotdeploy を許可しない。`gui_gate.py` に v12g1 / v12g4 の台本 (W3 の座標) を追加。役割分担の是正 (ユーザー指摘): ビルド・配備・CUI 実行はローカル AI、PM は GUI のマウス操作列と目視判定のみ |
| 2026-09-06 | **C4 コーダーの調査で真因判明 → ext2 のバグ修正** (`595b8a9`、マージ `4b5383e`): `ext2_free_all_blocks` が間接表を `ext2_g_aux` に置き、`ext2_free_block` が同じバッファにビットマップを読むため他ファイルのブロックを解放 → 12KB 超の上書きでファイルが相互リンク (テストアプリのヘッダの entry が化けて EIP 0x50C639)。POLICY_DEBUG §4-24。WSL 側の作業イメージは `e2fsck -fn` でほぼ健全 (ビットマップ 2 ブロックの食い違いのみ)、ゲストの損傷は Windows 側コピーにしか無く配備で消える。**ローカル AI で `make all` → 配備** (以後この経路)。libos32gui の `dbg_print` を `%s` に (kprintf は `%.*s` 非対応)。`/api/key` の text は 8 文字ずつ (raw リング 32 本)。**実機 (PEGC)**: **G1 通過** (`gui_gate.py v12g1`: Start / Programs / 右クリック menu / Run... で gui_demo 起動 / taskbar の窓ボタン 2 つとクリックでの前面化 / 時計の分更新)。**G4 通過** (`v12g4`: gui_demo → Run... で v12_api_test に置換 (sticky Quit)、アプリ発 `session_launch` で gui_demo に戻る、CUI mode → 日本語確認 → Yes → CUI で `GUI=0` 永続化、アプリ発 `session_switch_cui` も CUI へ、**Shut Down → 確認 → halt 画面で `sys_halt` に留まり復帰しない**)。**G2 の否定試験**: キー 5 `open2=FULL open3=FULL r=1 again=STALE bad=STALE` ✓、キー 7 ✓、キー 6 は Pointer が畳まれモーダル中は入力がアプリに届かないため溢れず (`ovf=0`) → タイマで溢れさせる版を C4 に依頼中。CTRL+STOP でモーダル所有アプリを畳んだ後の WM ダイアログも OK。**C5 (File Manager) を投下** |
| 2026-09-06 | **G2 通過**: キー 6 をタイマ 8 本 + 生 OP_POLL でリングを 128 に固定する版に変更 (C4 `2c40fb0`)。実機 `6: r=1 ring=128 ovf=1 dropped=131` = リング満杯のままダイアログを完了しても結果が保持され、空いた後に Modal event が届き `modal_result` が取れる (§7.5 / M3)。OK は最初の 2 秒 (X3 が回る相) に送る必要があり、静止相ではキーはカーネルの raw リング (32 本) で落ちる — これは設計どおり (`dropped` に計上、OVERFLOW 通知)。**G0〜G2、G4 通過。残り G3 (C5) と G5** |
| 2026-09-06 | **C5 (`ab38743`) をマージ** (feat/gui `d0f2dda`)、`filer` を登録 (`010233a`)、ローカル AI で配備。**G3 の実機 (PEGC)**: Start → File Manager 起動、2 ペイン + icon16 + size/type + パス行、New Folder (+「already exists (-5)」)、Rename、Delete (Yes/No → 「directory removed」)、Copy (「copy done」、CUI で 14540B 一致)、ディレクトリの Copy 拒否 (-8)、非空 rmdir → 「directory not empty (-7)」、右クリック menu がクライアント端で内側に収まる、Move (same-FS rename)。**残り**: 154KB のコピー、`.bin` の Run → filer 終了 → アプリ起動 (1 回目は起動が見えず要再試験)。**不具合 2 件を C5 に差し戻し**: (1) ツリーが折り畳み状態だとクリックで展開 / 移動しない (RETURN では展開する)、(2) Move に確認が無くディレクトリも移動できる → 私の行ずれクリックで **`/boot` を `/tmp/moved.bin` へ 2 回 Move してしまい CUI の `mv` で復旧** (vmkernel が /boot に無いと次回起動不能)。Move とディレクトリの Rename に Yes/No を付ける。**教訓**: 破壊的操作を含む台本は、選択行をスクリーンショットで確認してから確定キーを送る (パス行に選択名を出す改善も依頼) |
| 2026-09-07 | **C5 差し戻し 2 件をコーダーが修正 (`03e0dcf`、マージ `ca90d21`)**: ツリー左クリックは `on_raw` で印 → 選択が変わらなければ `after_commit` で `list_selection` を見て開閉 / 移動 (最終行より下の余白は `tree_row_hit` で除外)、Move と ディレクトリ Rename に Yes/No (`PEND_MOVE_CONFIRM` / `PEND_RENAME_CONFIRM`) を挟み No なら対象を捨てて「cancelled」、パス行に `[選択名]`。ローカル AI で `make all` + hotdeploy (42276 B)。**実機 (PEGC) で G3 の残りを通過**: 根の `[+]` クリックで展開、同じ行の再クリックは無害、余白クリックは無反応、`m` → 「Move g.bin ?」→ No で cancelled / Yes → Input → `/tmp/gm.bin` へ移動、`r` (ディレクトリ) → 「Rename directory tmp ?」、**154KB コピー** (`/bin/gshell.bin` → `/tmp/g2.bin`、進捗「copying... 81920 B」→「copy done」、CUI の `diff` で一致 153956 B)、**Run `.bin`** (`/usr/bin/gui_demo.bin` を RETURN → filer が退いて gui_demo の窓 2 枚 + taskbar)。ダイアログは窓基準の位置 (Yes (277,250) / No (362,250))。**新たな不具合 2 件を同じコーダーに依頼中**: (A) 右ペインのキー操作 (HOME / ROLLDOWN / DOWN) の再描画が 1 周遅れる (次のイベントまで画面が変わらない)、(B) パス行の `[選択名]` が右ペインのクリック選択に追随しない。修正後に再確認して G3 を閉じ、G5 へ |
| 2026-09-07 | **G5 前半**: (1) **ESC / 上部バー / F1〜F5 を製品から撤去** (コーダー `ecb656d`、マージ `6a8e62c`): `DEBUG_SHORTCUTS=false` (デバッグ施設として残置、`.text` 153912 → 152308)。上部バーは work_area の外の overdraw だったので回収する高さは無し。`gui_gate.py` の CUI 復帰を Start → CUI mode → Yes に、v11 の起動を Run... に。W3 §4.1 / CONTRACTS S6 / POLICY_DEBUG §4-23 更新。(2) **打鍵の反映が 1 周遅れる件** (コーダー `59a8223`、マージ `f7c44d8`): 真因は U3 ループが自分で宣言した damage をその周で受け取れない (Paint は次の OP_POLL でしか導出されない、CONTRACTS G4「次の周」) → damage を出した周はもう 1 度 POLL。パス行の追随は `repaint_sel()` で修正。ローカル AI で `make all` → **NHD 配備** (shlib 93204 / gshell 152352 / filer 42340)。**実機 (PEGC)**: ESC で CUI に落ちない・上部バー無し ✓、パス行の追随 ✓、`gui_gate.py v11 / v12g1 / v12g4` すべて通過 (新しい CUI 復帰経路で `GUI=0`)。**打鍵の 1 周遅れは配備後も再現** (HOME の結果が次のキー後に出る) → 同コーダーに gshell 側 (`emit_paints` / `wake_ready` の条件) の再調査を依頼中。9801 / Cirrus の回帰は最終バイナリで |
| 2026-09-07 | **打鍵の反映遅れの真因 = `libos32gui::draw::Painter::fill_solid` の per-pixel ループ** (1 px ごとに 6 回の境界比較 → 右ペイン ~108k px の塗りに 1.3 秒)。経緯: コーダーの 1 次修正 (アプリの二重 POLL `59a8223`、OP_INVALIDATE で Paint 即時配送 `c236267`、`page_vis_owner` `b2268c2`) はいずれも実機で効かず撤回。途中で見つかった実欠陥 2 件は採用: **Pointer を `moved` のときだけ ring に積む** (`b77c04e`、以前は OP_WAIT が眠らず毎 tick no-op commit していた、`gfx_counters` の idle 33 commits/s がこれ)、**起床判定と配送判定を `damage::deliverable_cand` に統合 + 配送不能 dirty の掃除** (`db5411f`)。決め手は実機計測: カーネル `gfx_counters` (0x1442a0) で「打鍵後 1.3 秒間 present ゼロ」、一時カウンタ `GSHELL_DBG` (`d581ac4`) で「Key 到着・OP_INVALIDATE・起床・2 回目 POLL はすべて同じ tick、present だけ 136 tick 後」、`/api/status` の EIP 連続サンプルで 11/11 が shlib 0x40a1b0〜0x40a1fe (`fill_solid`)。gui_demo が即時なのは塗り面積が小さいだけ。マウス移動で直って見えたのは時間の一致。**教訓**: 遅延は「起床経路」と決めつけず、まず present の有無 (`gfx_counters`) と EIP のサンプルで CPU の居場所を見る (30 秒で決まる)。コーダーに `fill_solid` の矩形一括クリップ + 行単位 fill、filer の選択変更を行単位再描画、`GSHELL_DBG` 撤去を依頼中 |
| 2026-09-07 | **G3 通過・G5 通過 (3 バックエンド)**。`fill_solid` の行 memset 化 + glyph / icon の行単位化 + filer の行単位再描画 (コーダー `1a5636f`、マージ `c453c18`)、`GSHELL_DBG` 撤去。ローカル AI で `make all` → NHD 配備。**PEGC**: filer の打鍵→present が DOWN 0.27 秒 / ROLLDOWN 0.45 秒 / HOME 0.44 秒 (HTTP 往復込み、前は 1.3〜2.0 秒)、描画は同一。**9801 (`gfxmode pc98` → reset、400 ライン)**: `v11` / `v12g1` / `v12g4` 通過。g4 が 2 回「Launch failed」になったのは台本の text 注入 (8 文字 / 0.3 秒) が planar 描画中の WM の drain を追い越して `v12_api_test.n` に欠けたため → `gui_gate.key` を 4 文字 / 0.35 秒に (`4ca433b`) して通過。v11 の Help 閉じるクリックは 400 ラインで座標がずれて外れる (台本の既知の制約、製品の不具合ではない)。**Cirrus (`set_wab.py on` → 配備 → `wab_relay=1` 640×480 → `off` に復旧)**: `v11` / `v12g1` / `v12g4` 通過、filer の打鍵 0.27 / 0.42 秒 (`present_bytes` 0 = エンジン経路)、CUI 復帰で `wab_relay=0`。**Shut Down (halt)** を最終バイナリで再確認 (`v12g4 --halt`)。`make check` 通過 (ローカル AI)、`check-gui-proto` 一致 (コーダー)。POLICY_DEBUG §4-25 / CLAUDE.md に「present と EIP を測ってから疑う」を記録。**残り**: ユーザーレビュー → feat/gui を main へ `--no-ff` マージ |
