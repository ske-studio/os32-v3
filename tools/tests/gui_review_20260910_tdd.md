# GUI レビュー指摘 2 件 (2026-09-10 第 2 回) — TDD 記録

対象レビュー: main `d739494` (GUI v1.2) に対する独立レビュー第 2 回。
第 1 回の 3 件は `c7d64e5` / `33c13dd` (feat/gui、main へは未マージ) で対応済み。

---

## [P2] 右クリックでボタン実行・チェック切替が起きる

**指摘**: WM は右ボタンのイベントを配送するが、`libos32gui` は `b.button` を
確認せず左右とも通常の押下・解放として処理する。

**到達可能性の確認** (ソース):

- `userland/gshell/src/input.rs:489-493` — `rdown_edge` / `rup_edge` で
  `forward_button(st, mx, my, MOUSE_BTN_RIGHT, ..)` を呼ぶ。
- `input.rs:752` `forward_button` → `ring::ev_button(down, .., button, ..)` が
  `GuiEvtButton.button` に 1/2 を載せる (契約 D4)。プロデューサはここ 1 箇所だけ。
- `userland/rust/libos32gui/src/app.rs` の `GUI_EV_BUTTON` アームは
  `b.x` / `b.y` しか読まず、`widget::on_button_down/up` → `emit` → `on_click` /
  `on_toggled` へ無条件に流す。

→ **指摘は正しい**。前面窓のクライアント上で右クリックすると、その座標に
あるボタンの `on_click` とチェックボックスの切替が起きる。

**修正**: `app.rs` のウィジェット配送を `b.button == MOUSE_BTN_LEFT` の内側へ。
右ボタンが要るアプリは `on_raw` (この `match` より**前**に必ず呼ばれる、
`app.rs:222`) で受けられるので、機能は失われない。

**試験**: `tools/tests/test_gui_button_dispatch.py` (`check-tools-host` に登録)。

> **これは構造ガードであり挙動試験ではない。** libos32gui にはホスト実行の
> 足場が無く (gshell の `host/integration.py` のような harness が無い)、
> 今回それを新設するのは指摘 1 行の修正に対して釣り合わないと判断した。
> 実ソースから `GUI_EV_BUTTON` の match アームを波括弧の対応で切り出し、
> ウィジェット配送が `MOUSE_BTN_LEFT` の判定より内側にあること、
> `MOUSE_BTN_LEFT` の値が C ヘッダ (SSoT) と一致すること ([C4])、
> `on_raw` が `match` より前にあることを見る。
> **エミュレータでの右クリック再現は未実施。**

RED (修正前):

```
FAIL Button アームがボタン種別 (b.button) を見ている
FAIL Button アームに MOUSE_BTN_LEFT の判定がある
FAIL widget::on_button_down が MOUSE_BTN_LEFT の判定より内側にある
FAIL widget::on_button_up が MOUSE_BTN_LEFT の判定より内側にある
FAIL emit( が MOUSE_BTN_LEFT の判定より内側にある
SUMMARY 5 FAILED
```

GREEN: `SUMMARY PASS` (12 項目)。

---

## [P2] Enter 連打で、日本語の確定文字がダイアログ結果から抜ける

**指摘**: 確定用と決定用の Enter が同じ入力処理周期に入ると、最初の Enter で
確定文字を一時保存し、次の Enter でそれを反映する前にダイアログが閉じる。

**到達可能性の確認** (ソース):

- `input.rs:377` — `fep::flush_text(st)` はキー吸い出しループの**外**、
  周期の末尾で 1 回だけ呼ばれる。ループは生キュー (`kbd_trygetrawkey` /
  `st.pending_raw`) が空になるまで回るので、連打した 2 つの Enter は
  **同じ周期**に入る。
- Enter #1: `fep::feed` が `Fed::Consumed` を返し `continue` (`commit_len > 0`)。
- Enter #2: `fep::feed` は `Fed::Pass` (変換中でない) → `modal::on_key` が
  RETURN を受けて `finish` → ダイアログが閉じる。field は空のまま。
- そのあと `flush_text` が走るが `modal::is_open()` は既に false なので、
  `fep.rs:461` の分岐でアプリ配送側へ落ち、確定文字が背後のアプリへ `Text`
  として流れる (宛先が無ければ捨てられる)。

→ **指摘は正しい**。結果が空になり、確定文字はアプリへ漏れる。

**修正**: `fep::feed` が消費しなかった打鍵を配る**前に** `fep::flush_text(st)`
を呼ぶ。モーダル側 (`input.rs`) と非モーダル側の 2 箇所。非モーダル側では
確定文字の `Text` がこの打鍵の `Key`/`Text` より先にリングへ積まれ、
順序も保たれる。

**試験**: `userland/gshell/host/wm_tests.rs` の
`double_enter_keeps_fep_commit_in_the_input_dialog_result`
(`make check-gshell-host` = `host/integration.py`)。
記録当時の置き場は `host/terminal_tests.rs` / `make check-t5b-host` で、
T5b 撤去 (2026-09-10) のときに現在の名前へ移した。
実物の `input.rs` / `fep.rs` / `modal.rs` を、モックした KAPI
(`ime_feed_key` / `ime_is_active` / `kbd_trygetrawkey`) の上で動かす**挙動試験**。

- `mocks.rs` に生キュー (`RAWKEYS`) と FEP 台本 (`IME_SCRIPT`) を追加。
  `kbd_trygetrawkey` は既定で空 = -1 なので既存試験に影響しない。
- 台本 `[0x61, 0x62, 0x00, 0x100]` = 1 回目の Enter で "ab" を確定、2 回目は素通し。
- Enter を 2 つ**同じ周期**に積んで `input::capture(st, Ctx::Wait)` を 1 回。
- 検査: ダイアログが閉じたこと / `launch_pending` が立つこと /
  結果が `"ab"` であること / アプリのリングに `Text` が 1 件も無いこと。
  リング件数そのものは閉じるときの再描画でも動くので、`Text` の中身だけを見る。

RED (修正前): `28 passed; 1 failed` — 上記 4 検査のいずれかで停止。
GREEN (修正後): `29 passed; 0 failed`。

---

## 未実施

- どちらの指摘も**エミュレータでの再現試験はしていない** ([V4])。
  レビュー側も「ソース上の処理経路を確認したレビュー」と明記している。
- `feat/gui` → `main` のマージは未実施。第 1 回の P1 (filer のデータ損失) を
  含め、main にはまだ 5 件とも入っていない。
