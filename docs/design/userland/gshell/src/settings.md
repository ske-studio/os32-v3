# userland/gshell/src/settings.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S4・TASK_S5 (S5-W) の契約を今のソースと照らして要約
ソース: `userland/gshell/src/settings.rs` (`load`・`consume`・`save`・`on_ok`・`request_open`・`status_lines`)・`userland/gshell/src/modal.rs` (設定ダイアログ `layout_settings`・`settings_key`・`draw_clipped`)・`userland/gshell/src/taskbar.rs` (`format_clock`・`clock_rect_for`)

gshell は設定レジストリの消費者で、`gshell` scope の key を読み書きする。library の約束 (接続は同時 1 本、open から close の間に yield しない、X4・IRQ・描画 callback から呼ばない) は `docs/design/userland/lib/cfg/libos32cfg.md` の「設定レジストリの全体の約束」。

| key | 意味 | 既定 | 範囲外の扱い |
|---|---|---|---|
| `desktop/color` | 背景のシステム色 index | 12 (`GUI_COLOR_DESKTOP`) | 0〜15 以外は既定 (`clamp_color`) |
| `taskbar/clock_24h` | 1 = `HH:MM`、0 = `h:MM AM/PM` | 1 | 0・1 以外は既定 (`clamp_clock`) |
| `font_set` | 0 = 標準、1 = KCG のみ | 0 | `clamp_font_set` |
| `font/gen` | font 設定の世代。`font_set` を保存するたびに再読込値 + 1 を同じトランザクションで書く | 0 | — |

## DB に触る場所は top-level だけ

- `cfg_*` を呼ぶのは `standalone_loop` (owner 1) から 1 周 1 回呼ぶ `consume` だけ。X4 (`Ctx::Pump`)・X3 (`Ctx::Wait`)・`handler`・メニューの文脈・`finish_wm` の中では 1 本も呼ばない。
- Start → `Settings...` は予約 (`request_open`) を立てるだけ。予約がある間 `pending` が真になり、`multiapp::should_park` と `pick_poll` の門が走っているアプリに譲らせる。
- `consume` の順は固定: (1) `Req::Save` はモーダル枠と関係なくその周回で DB 操作と適用まで済ませ、結果の通知だけを残す。(2) 通知は枠が空いている周回でだけ出す (`open_wm_message` の戻りを見て、開けなければ保持)。(3) `Req::Open` も枠が空いている周回でだけ `load` してダイアログを開く (開けなければ予約を戻す)。アプリの `MODAL_OPEN` は拒否しない。

## 読み込み (`load`)

1. `cfg_open(&db, 0)` → `cfg_get_int` を key ごとに → `cfg_close` を閉じきる。`cfg_open` が負、または 0 を返して NULL を置いたら `status = -1` (`STATUS_OPEN_FAILED`) で close を呼ばない。
2. `cfg_status`・`cfg_last_sqlite`・`cfg_schema_version` は **get の後** に採る (読む途中の `CFG_ERROR` への変化を取り逃がさない)。
3. 取得値を採るのは status が `CFG_OK` か `CFG_VERSION` のときだけ。それ以外は全 key を既定へ倒し、診断 (status・sqlite・版) は保つ。
4. close の失敗は `close_error` に残し、値は使う。`close_error` は後で再読込が成功しても消えない。

## 起動時の通知

OK かつ close 成功なら出さない (kprintf の 1 行 `gshell: cfg OK color=<n> clock24=<n> load=<n>t` だけ)。それ以外は 1 回だけ出し、閉じたら二度と出さない。文言は `Settings: MISSING - defaults in use. Run 'cfg init' in CUI.`・`Settings: CORRUPT - defaults in use`・`Settings: VERSION <n> (read only)`・`Settings: ERROR sqlite=<n> - defaults in use`・`Settings: ERROR open=<rc> - defaults in use`。close も失敗していれば `; close failed (<code>)` を継ぐ (通知は 1 本)。

## 設定ダイアログと保存

- 開いた周回で `load` した値が再読込値 (編集の起点)。適用値 (`GuiState::cfg`) は保存が成功するまで動かさない。
- 操作: `→`・`←` で色が 0〜15 を循環、`↓`・`↑` で行移動 (端で止まる)、SPACE で時計を切り替え、ESC・Cancel は何も書かない。TAB は焦点も編集値も動かさず閉じもしない (RETURN は常に OK)。マウスは別の行のクリックで選択、同じ行の再クリックで値が進む。
- OK (`on_ok`): 編集値が再読込値と同じなら `cfg_open` を呼ばず `gshell: cfg write skipped` を kprint してメッセージも出さない。再読込の状態が `CFG_OK` でなければ書かずに `cannot save: <状態>`。それ以外は変わった key のマスクを立てて `Req::Save` を予約する。
- `save`: `cfg_open(&db, 1)` → `cfg_status` が `CFG_OK` でなければ 1 byte も書かずに close して `cannot save: <状態>` → `cfg_begin` → 変わった key だけ `cfg_set_int` (color → clock → font_set・font/gen の順) → `cfg_commit` → `cfg_close`。rollback は呼ばず close に任せる。
- 失敗は段ごとに `save failed: <open|begin|set|commit> (<code>)` で、適用値は動かさない。commit が成功して close だけ失敗したら、適用値を更新してから `saved, but close failed (<code>)` を出し `close_error` に残す。

## 描画

- 状態行は `status_lines` で最大 3 行: `settings.db: <状態>` (常に)、`close failed (<code>)` (`close_error` があるとき)、`load <n>t save <n>t` (常に)。
- 版面の幅は実表示幅から出す。色の行は常に説明文つきの版と最大桁 (15) で測る。最後に `draw_clipped` で行矩形に入る文字数で切り、枠の外へ 1 画素も出さない。
- パレットのリース中 (2 色) はダイアログが `TEXT`・`WINDOW` の 2 色しか置かず、色見本を描かずに数値と `(preview off: palette leased)` を出す。`desktop::fill` もリース中は従来の市松だけ。
- 時計: 24h は `00:00`・`23:59`、12h は `12:00 AM`・`1:05 PM` (時は空白詰めしない)。幅 = 文字数 × 8 + 8、右端は文字数によらず揃う。縮む更新では旧矩形も dirty に入れる。
- Start メニューの root は 6 項目・この順: `Programs`・`File Manager`・`Run...`・`Settings...` (index 3)・`CUI mode` (4)・`Shut Down` (5) (`startmenu::root_label`、`gui_gate.py` の `ROW_*` と対)。

## 検査

`settings_tests.rs (旧版 b32248ea9df8)` (`make check-gshell-host`、`integration.py (旧版 14d0fb10090f)` が rustc で組む)。`cfg_*` は `host/mocks.rs` の贋物 `CfgFake` で、DB を動かす操作 (`Open`・`GetInt`・`Begin`・`SetInt`・`Commit`・`Rollback`・`Close`) の順を記録する。S09b・S14 はスケジューラの関数を順に直接呼ぶ試験で、`handler` → 実 park → `standalone_loop` の実遷移は通していない (ゲスト受入の担当)。
