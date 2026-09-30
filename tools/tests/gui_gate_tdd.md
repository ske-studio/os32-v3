# gui_gate.py の「GUI に入れたか」— ホスト試験の記録 (RED→GREEN)

- 経緯: [`docs/archive/settings/TASK_S5.md`](../../docs/archive/settings/TASK_S5.md) §6 の
  R2 の予備調査 (2026-09-29)、[`docs/POLICY_DEBUG.md`](../../docs/POLICY_DEBUG.md) §4-31。
  台本が rshell を閉じずに `os32gui` を打つと、4 文字ずつの text が `os32` / `gui` の
  2 コマンドになって GUI に入らず、`leave_gshell` の `ver` は CUI でも通るので
  **CUI のまま RESULT: OK** になっていた。
- 実行: `python3 -B tools/tests/test_gui_gate.py [--mutate]`
  (`make check-gui-gate-host`、`check-changed` / `check` で `--mutate` 付き)
- 対象: `tools/gui_gate.py` (実物を `import`。`post` / `get` / `_cmd_raw` / `time` を偽物に差し替える)

## 0. この試験が触らないもの

NP21/W・HTTP・実時間に触らない (時計は偽物、0.4 秒で終わる)。**実際のゲストで ESC が
rshell を閉じるか、`/api/status` の `scrn_ymax` / `grph_disp` がこの値になるかは
ここでは分からない** — NP21/W で台本を回して確かめる ([V4])。値は予備調査の実測
(CUI `scrn_ymax 400 grph_disp 0`、PEGC の GUI `scrn_ymax 480 grph_disp 1`) に合わせた。

## 1. 偽のゲスト (`FakeGuest`)

- rshell は `depth` 段重なる。ESC は内側 1 段を閉じて `[Remote shell closed]` を出し、
  最後の段が閉じたときだけプロンプトを出す (外側の rshell は黙っている)。
- rshell が有効な間の text は **1 回の POST が 1 コマンド** (§4-31 の罠そのもの)。
- CUI の text は行に溜まり RETURN で実行。`os32gui` で GUI へ (`gui_ok=False` で入らない)。
- GUI では Start → `ROW_CUI` → Yes (410, H/2+11) で CUI へ戻り、rshell が 1 段で立つ。

## 2. RED (直す前の gui_gate.py = feat/gui `0f0bc25d`)

`case_scenarios` を元の実物に当てた結果 (10 件の FAIL):

- `v11` / `v12g1` / `v12g4`: 「GUI に入っていない (0)」「GUI に入らないのに OK」
  「CUI にパスを打ち込む」— 元の不具合 (rshell 有効のまま始めると CUI のまま OK) を再現。
- `v12g4 --halt`: 2 回目に GUI へ入れない。

(`fresh` / `gui_entered` / `close` / `begin` は新しい関数の試験なので、元の実物では
属性が無く落ちる。)

## 3. GREEN

`SUMMARY 5/5 PASS` (fresh / gui_entered / close / begin / scenarios)。

## 4. 変異 (否定側、14 本すべて RED)

| # | 壊し方 | 当たる試験 |
|---|---|---|
| 1 | `gui_entered` が常に True (元の不具合) | begin, scenarios, gui_entered |
| 2 | `grph_disp` を見ない (9801 の CUI は 400 で GUI と同じ高さ) | gui_entered |
| 3 | `scrn_ymax` を見ない | gui_entered, begin |
| 4 | `begin_gui` が rshell を閉じずに `os32gui` | begin, scenarios |
| 5 | 印が出なくても閉じたと言う | close, begin |
| 6 | 重なった rshell の外側を閉じない | close |
| 7 | 印の数を見ない (末尾が同じ形の新しい印を見落とす) | fresh |
| 8 | 末尾の変化を見ない (画面が流れたときの新しい印を見落とす) | fresh |
| 9 | 印が末尾に無くても数える | fresh |
| 10 | GUI に入れなかった後に rshell を戻さない | begin |
| 11〜13 | v11 / v12g1 / v12g4 が入口の NG を無視する (元の不具合) | scenarios |
| 14 | `--halt` の 2 回目で rshell を閉じない | scenarios |

## 4b. 入口の NG で GUI に残る (2026-09-29 追加)

- 経緯: Cirrus 試験で入口が「GUI に入ったが `scrn_ymax` が `--h` と一致しない」で NG になり、
  `restore_rshell()` が CUI の前提で打った `rshell` が gshell に入ってゲストが GUI に残った
  (手で `leave_gshell` を呼んで復旧)。
- 直し: NG の後始末を `back_to_cui(st, h)` にした。`grph_disp == 1` なら**実際の高さ**
  (`scrn_ymax`) の `Mouse` で `leave_gshell` を通して CUI へ戻す (rshell の復旧も
  `leave_gshell` がする)。GUI に入っていなければ従来どおり `restore_rshell`。
- 試験 (`case_begin`): 偽ゲスト h=400 に `begin_gui(480)`、h=480 に `begin_gui(400)` —
  NG・CUI に戻る・rshell 1 段・GUI に text を打たない。GUI に入らない場合はマウスを押さない。
  偽ゲストは GUI 中の text (`gui_text`) と押下の回数 (`clicks`) を数える。
- RED (直す前の実物 = feat/gui `ab3a3a65`): `begin` の 4 件 (GUI に残す・rshell を戻さない・
  `['rshe', 'll']` を GUI に打つ・逆向きも同じ)。GREEN: `SUMMARY 5/5 PASS`。

| # | 壊し方 | 当たる試験 |
|---|---|---|
| 10 | (対象を `back_to_cui(st, h)` に付け替え) NG の後に何もしない | begin |
| 15 | `grph_disp == 1` でも CUI の前提で `restore_rshell` (直す前の挙動) | begin |
| 16 | GUI から抜けるのに `--h` の座標を使う | begin |
| 17 | GUI に入っていなくても `leave_gshell` を通す | begin |

## 4c. Cirrus (WAB 中継) の GUI を認識しない (2026-09-29 夕 追加)

- 経緯: NP21/W Cirrus (`np21x64w-cirrus.ini`、feat/gui `44bd0fe1`) で道具そのままの `v11` が NG。
  gshell 中も 98 の表示レジスタは `scrn_ymax 400 grph_disp 0` のままで、画面は `/api/status` の
  `wab_relay 1`・`wab_height 480` が表す。`gui_entered` が NG と読み、`back_to_cui` が `grph_disp == 0` の
  枝で `rshell` を gshell に打ち込んだ (試験担当が踏んだ。wt/r2-cirrus2 の TASK_S5 §6)。
- 直し: `status()` が `wab_relay` / `wab_width` / `wab_height` を読む。`gui_entered(st, h)` =
  (`scrn_ymax == h` かつ `grph_disp == 1`) または (`wab_relay == 1` かつ `wab_height == h`)。
  `back_to_cui` は新しい `gui_height(st)` (WAB 中継中は `wab_height`、でなければ `grph_disp == 1` の
  `scrn_ymax`、どちらでもなければ None) で GUI に居るかと実際の高さを決める。`Shots.take` は撮影ごとに
  名前・`X-Screen-Source`・寸法を `outdir/shots.json` に書き足す。
- 偽ゲスト: `FakeGuest(cirrus=True)` は GUI 中も `scrn_ymax 400 grph_disp 0`、`wab_relay` = GUI 中だけ 1、
  `wab_height` = 画面高 (CUI でも残す — CUI の実測値は無いので、`wab_relay` を見ない判定を落とす側に置いた)。
  GUI 中の撮影は `X-Screen-Source: wab`。
- 試験: `gui_entered` に Cirrus の 5 件 (入った / 高さ違い 2 向き / 中継なし / 値なし)。`begin_cirrus` =
  入った (OK) / 入らない (NG・rshell 1 段・マウスを押さない) / 高さ違い 2 向き (NG・CUI に戻る・rshell 1 段・
  GUI に text を打たない)。`shots` = CUI と GUI の撮影で `shots.json` の src が `fake` / `wab`。
  `scenarios` は 3 台本を Cirrus でも回す。
- RED (直す前の実物 = feat/gui `44bd0fe1`): `gui_entered` 1 件、`begin_cirrus` 7 件 (`['rshe', 'll']` を
  GUI に打つ 3 件を含む)、`shots` 1 件 (shots.json 無し)、`scenarios` 6 件 (Cirrus の 3 台本が NG・GUI に残る)。
  `SUMMARY 3/7 PASS`。GREEN: `SUMMARY 7/7 PASS`。
- 変異: 1〜3 と 15・17 は対象の行を新しい形に付け替えた (15 = `gui_height` の `grph_disp` 枝を消す、
  17 = `real_h is not None` を常に真)。追加:

| # | 壊し方 | 当たる試験 |
|---|---|---|
| 18 | WAB 中継を見ない (Cirrus の GUI を NG — 踏んだ不具合) | gui_entered, begin_cirrus, shots, scenarios |
| 19 | `wab_relay` を見ない | gui_entered, begin_cirrus, scenarios |
| 20 | `wab_height` を見ない | gui_entered, begin_cirrus |
| 21 | `status()` が `wab_height` を読まない | begin_cirrus, shots, scenarios |
| 22 | `gui_height` が WAB 中継を見ない (CUI と読んで `rshell` を gshell に打つ) | begin_cirrus |
| 23 | Cirrus で抜けるのに `scrn_ymax` (400) の座標を使う | begin_cirrus |
| 24 | 撮影の `X-Screen-Source` を記録しない | shots |

  24 本すべて RED。24 は最初 GREEN (見逃し) だった — `case_shots` が本番の回の `shots.json` を読んでいた
  ので、撮影先を毎回新しい一時ディレクトリにした。

## 5. 未検証 (NP21/W で見ること)

- 実ゲストで ESC 1 回目に `[Remote shell closed]` が 5 秒以内に tvram に出ること。
- CUI のプロンプトでの 2 回目の ESC が画面を変えないか、変えても末尾 2 行に印を残さないこと
  (残すと重なりの判定で余分に ESC を 1 回送るだけ — 上限 2 回で止まる)。
- 9801 (`--h 400`) と PEGC (`--h 480`) の両方で、GUI 中の `grph_disp == 1` と `scrn_ymax == --h`。
- 高さ違いで GUI に入ったとき、実際の高さの座標で Start → CUI mode → Yes が当たり CUI へ戻ること
  (Cirrus で `scrn_ymax` が実際の画面高を表すかを含む)。
- Cirrus で直した道具そのまま (包み無し) の `v11` / `v12g1` / `v12g4 --h 480` が RESULT OK になること、
  `shots.json` の src が GUI 中 `wab` になること。CUI での `wab_height` の値 (偽ゲストは画面高のまま置いた)。
- Cirrus の高さ違い (`--h 400`) で `wab_height` の座標の `leave_gshell` が CUI mode → Yes に当たること。
