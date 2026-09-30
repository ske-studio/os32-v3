# W3: デスクトップ / セッション状態機械

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**
> レーン: W / 前提: K5  
> 親: [TASKS.md](../../tasks/gui/v12/TASKS.md) / 契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md) V12-S / V12-D  
> 排他: `userland/gshell/**`

## ゴール

v1.1 の gshell を、taskbar / Start / launcher / clock / CUI 切替 / system halt を持つデスクトップへ拡張する。

最重要条件は、**アプリ実行中の WM 文脈から nested `exec_run()` を絶対に呼ばない**こと。

## 1. モジュール

既存 `desktop.rs` を拡張し、必要なら次を追加する。

```text
desktop.rs     背景 + taskbar + work area
startmenu.rs   Start / Programs / desktop menu
session.rs     SessionAction / sticky Quit / launch handoff
```

## 2. Taskbar

- 画面下端 24px 固定。
- WM 自身の UI として描画し、アプリ Window 16 / Surface / SHM slot を消費しない。
- 左: Start。
- 中: 現在の外部アプリが持つ visible top-level window のボタン。
- 右: 時計。
- taskbar 領域の入力をアプリへ配送しない。
- window button click は process switch ではなく `raise + focus`。

作業領域:

```text
work_rect = (0, 0, screen_w, screen_h - 24)
```

- 640x400 -> 640x376
- 640x480 -> 640x456

新規配置とドラッグ確定時は outer rect を work area へクランプする。既存ウィンドウが一時的に外へ出ていること自体は fault にしない。

## 3. Clock

- `sys_time()` で HH:MM を表示。
- 1 秒より細かい更新は不要。
- 表示文字列が変わった時だけ時計矩形を dirty。
- clock 更新だけで `composite_full()` / fullscreen present しない。

## 4. Start menu

最低項目:

- Programs
- File Manager
- Run...
- CUI mode
- Shut Down

`Programs` は `/usr/bin` の `.bin` を列挙する。最大 96 件。超過時は先頭 96 件 + `...` 表示でよい。

- 列挙は menu open 時の X3 で 1 回だけ。
- 開いている間は cache。
- X4 で `sys_ls` しない。
- menu close / session change で cache を捨ててよい。

File Manager は `/usr/bin/filer.bin` を起動要求する。

Run... は W4 の Input dialog を使い、絶対パスを入力して `LAUNCH` する。v1.2 では引数列や PATH 検索は対象外。

CUI mode / Shut Down は **WM 内蔵の確認ダイアログ (Yes / No) を経てから** SessionAction を立てる
(契約 S6)。1 キーで無言のまま CUI へ落ちる経路は作らない。

### 4.1 デバッグ用の残置と撤去 — **G5 で撤去済み (2026-09-07)**

v1.1 の ESC 即時切替と上部バー (`OS32 GUI shell ESC:CUI F1..F5`) は開発中のデバッグ用として残し、
**G5 (v1.2 完成) で撤去する** (ユーザー決定 2026-09-06) — 完了。`lib.rs` の
`DEBUG_SHORTCUTS` を `false` に倒し、出荷形からは次の 3 つが消えた:

- ESC の即時 CUI 切替 (`input::standalone_key`)
- 上部の手引きバー (`desktop::draw_hint` / `hint_rect` は空矩形を返し、そこは素の背景)
- F1〜F5 のランチャ (`LAUNCH_APPS` / F5 のファイル選択ダイアログ)

CUI へ戻る経路は Start → "CUI mode" → 確認ダイアログ → SessionAction (`GUI=0` を永続化) だけ。
F1〜F5 のランチャは Start → Programs / Run... で置き換わった。

コード本体は `DEBUG_SHORTCUTS` の下に**デバッグ設備として残してある** (実機で Start が
操作できないほど壊れたときの脱出路)。`true` に倒すのは手元の調査中だけで、コミットしない。
手引きバーは元から作業領域 (`wm::work_area` = 画面 − taskbar) を取っていないので、撤去で
空く座標や再配置は無い。ゲート台本 `tools/gui_gate.py` も ESC / F1〜F5 を使わなくなった
(`leave_gshell` が Start → CUI mode → Yes を踏む)。

### 4.2 右ボタン

v1.1 の `input.rs` は左ボタンのエッジしか扱わない。右ボタンの down/up エッジを取り込み、
`wm_owns_edge` と同じ領分判定で、デスクトップ / taskbar 上は WM の context menu、前面窓の
クライアント上はアプリへ `Button{button=2}` として配送する。X4 で WM の領分のエッジを
消費しない規則 (POLICY_DEBUG §4-22) は右ボタンにも適用する。

## 5. SessionAction

1 件だけ保持する。

```text
NONE
LAUNCH(path)
SWITCH_CUI
SHUTDOWN
```

### 5.1 gshell 自身からの要求

Start menu の選択は直接 SessionAction を立てる。

### 5.2 外部アプリからの要求

K5 `GUI_OP_SESSION_REQUEST` を X1 で受ける。

- request を検証。
- `GuiString` を gshell 私有固定バッファへコピー。
- pending が既にあれば `ERR_FULL`。
- ここでは VFS / cfg / exec をしない。

## 6. アプリが動いている場合

SessionAction が立ったら、現在 owner に対応する slot に `quit_pending` を立てる。

Quit reason:

- LAUNCH -> `REPLACE_APP`
- SWITCH_CUI -> `SWITCH_CUI`
- SHUTDOWN -> `SHUTDOWN`

`GUI_EV_QUIT` がリング満杯なら捨てない。`quit_pending` を残し、以下の機会で再度 append を試す。

- `OP_POLL` のイベント返却準備時
- X3 周期

成功してリングへ入った時点で `quit_pending` は消してよい。ただし SessionAction は owner が終了するまで残す。

アプリが Quit を無視する場合の自動 kill timeout は v1.2 では導入しない。CTRL+STOP で既存 kill 経路を使う。

owner が正常終了 / CTRL+STOP / fault kill で回収されたら、SessionAction は失わずトップレベルへ戻す。

## 7. トップレベル handoff

既存 `launch_app()` が戻った直後に SessionAction を評価する。

### LAUNCH

1. 旧 owner の窓 / timer / modal / slot が回収済みであることを確認。
2. palette / GFX を v1.1 と同じ手順で再確立。
3. pending path を NUL 終端の私有バッファへ変換。
4. `exec_run(path)`。
5. path/action を consume。

`exec_run()` が失敗した場合は desktop へ戻り、MessageBox 相当の WM owned notification を出す。失敗した path を無限 retry しない。

### SWITCH_CUI

1. current owner が無いこと。
2. cursor hide。
3. `ime_set_render(NULL)`。
4. `gfx_shutdown()`。
5. `/etc/system.cfg` を `GUI=0` に更新。
6. `sys_switch_shell("/sys/shell.bin")`。
7. gshell の main を return。

cfg 更新に失敗した場合は shell 切替を実行せず desktop へ戻し、エラーを表示する。永続設定と実際の shell が食い違う状態を作らない。

### SHUTDOWN

`sys_halt()` は 1 回の `hlt` で割り込み後に戻るため、1 回呼んで終了したつもりにしない。

1. current owner が無いこと。
2. cursor hide。
3. `ime_set_render(NULL)`。
4. `gfx_shutdown()`。
5. TVRAM を停止画面へ戻し、`System halted. Reset to restart.` を表示してよい。
6. `for (;;) { sys_halt(); }` に入り、通常コードへ二度と戻らない。

v1.2 の Shut Down は **電源 OFF ではなく system halt** と定義する。

## 8. Desktop context menu

右クリックで WM 内部の簡単な menu を出す。

最低項目:

- File Manager
- Run...
- Refresh Programs

popup Window ABI は作らない。WM 自身の overlay として描く。

## 9. X4 制約

X4 で許可:

- raw input 取り込み
- cursor sprite 移動
- session request / quit 再配送に必要な小さな state flag の更新

X4 で禁止:

- `sys_ls`
- `sys_open/read/write`
- `exec_run`
- cfg 更新
- menu 構築
- modal directory reload
- full composite

## 完了条件 (G1 / G4)

- `/api/mouse` で Start を開閉できる。右クリックでデスクトップ menu が出る。
- CUI mode / Shut Down は確認ダイアログの Yes を経ないと実行されない。
- (G5 で) ESC 即時切替と上部バーが撤去され、`/api/key` の ESC で CUI へ落ちない。**済 (§4.1)**
- taskbar window button で 2 窓の focus/raise が切替わる。
- 9801 400-line / PEGC,Cirrus 480-line で work area が正しい。
- clock 更新の present は時計矩形だけ。
- gui_demo -> Start -> File Manager で nested `exec_run()` 無しに置換できる。
- `gui_busy` を CTRL+STOP した後も予約済み SessionAction が続行される。
- CUI 切替後 `ver` が応答し `GUI=0` が永続化される。
- Shut Down は IRQ 後に desktop へ復帰せず halt loop に留まる。
