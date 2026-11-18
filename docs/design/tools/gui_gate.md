# tools/gui_gate.py — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S5 (§6 の R2 の予備調査と Cirrus の観測) の契約を今のソースと照らして要約
ソース: `tools/gui_gate.py` (`close_rshell`・`begin_gui`・`back_to_cui`・`leave_gshell`・`Shots`)・`tools/np21w_mcp/gui.py` (`gui_entered`・`gui_height`)

NP21/W の HTTP API で GUI の台本 (`v11`・`v12g1`・`v12g4`) を回す。画面の良否は人が撮影で見て、道具は「GUI に入れたか・CUI に戻れたか」を数値で照合する。

## 守ること

- **RESULT: OK は GUI に入った証拠でなければならない**。CUI のままでも `leave_gshell` の `ver` は通るので、入口で `/api/status` を照合しないと GUI に入らずに OK になる。
- rshell が有効なまま `os32gui` を打たない。rshell は入力が途切れるたびに 1 コマンドとして実行するので、4 文字ずつの text が `os32` と `gui` の 2 コマンドになる。

## 入口 (`begin_gui`)

1. `close_rshell`: ESC を送り、tvram の末尾 2 行に `[Remote shell closed]` が **この ESC で新しく** 出たことを確かめる (印が末尾にあり、かつ印の数が増えたか末尾が変わった)。前の回の印が残っているだけでは数えない。重なった rshell を閉じるため、印が出なくなるまで ESC を最大 2 回足す。最初の ESC で印が出なければ GUI へ入らずに NG。
2. `os32gui` を打って GUI へ入る (`enter_gshell`)。
3. `/api/status` を照合する (`gui_entered(st, h)`)。真になるのは次のどちらか。
   - 98 の GDC・PEGC: `scrn_ymax == --h` かつ `grph_disp == 1`。9801 (`--h 400`) は CUI と `scrn_ymax` が同じなので `grph_disp` が決め手。
   - Cirrus (WAB 中継): `wab_relay == 1` かつ `wab_height == --h`。このとき 98 の表示レジスタは CUI と同じ `scrn_ymax 400 grph_disp 0` のまま残る。
4. NG なら台本はそこで終わり、CUI に Run... のパスなどを打ち込まない。

## NG の後始末 (`back_to_cui`)

- `gui_height(st)` で GUI に居るかと実際の画面高を決める: WAB 中継中は `wab_height`、そうでなく `grph_disp == 1` なら `scrn_ymax`、どちらでもなければ GUI に居ない。
- GUI に居る (高さが `--h` と違うだけ) なら、**実際の高さ** の座標で `leave_gshell` を通して CUI へ戻す (rshell の復旧も `leave_gshell` がする)。CUI の前提で `rshell` を打つと gshell に打ち込まれてゲストが GUI に残る。
- GUI に入っていなければ `restore_rshell` (CUI で `rshell` を打ち、`ver` で確かめる)。マウスは押さない。

## CUI へ戻る経路 (`leave_gshell`)

Start → `CUI mode` (`ROW_CUI`) → 確認ダイアログ Yes が唯一の経路。`/etc/system.cfg` の `GUI=0` を永続化するが、台本は必ず CUI から始めて `os32gui` で入るので支障は無い。v11 は加えて最後に `wab_relay == 0` を見る。

## 撮影 (`Shots`)

`/api/screenshot?src=auto` で撮り、1 枚ごとに名前・`X-Screen-Source`・寸法を `--out` の `shots.json` に書き足す (途中で落ちてもそれまでの分は残る)。Cirrus の GUI 中は `wab` になり、98 の GDC の画面を撮っていないことを後から確かめられる。

## 検査

`tools/tests/test_gui_gate.py [--mutate]` (`make check-gui-gate-host`)。偽のゲスト (`FakeGuest`、`cirrus=True` で WAB 中継) と偽の時計で、NP21/W・HTTP・実時間に触らない。
実ゲストでの ESC の効き、`/api/status` の値、高さ違いのときの座標が当たるか、CUI での `wab_height` の値はここでは分からず、NP21/W で台本を回して確かめる。
