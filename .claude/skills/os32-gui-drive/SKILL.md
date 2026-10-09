---
name: os32-gui-drive
description: NP21/W で OS32 GUI の入力、起動、状態、出力リング、ASCII 文字読み取りと待ちを行い、受入の証拠を採る。CUI のコマンド実行は run-os32 を使う。
---

# os32-gui-drive — GUI を操作して証拠を読む

動作中のエミュレータを同時に操作するのは 1 人だけ。先に PM と使用区間を合わせる。
ツールは `tools/np21w_mcp/server.py`、共有実装は `tools/np21w_mcp/gui.py`。
このスキルはエミュレータの起動・配備を行わない。
CUI のコマンド実行は [run-os32](../run-os32/SKILL.md) を使う。

## 前提と GUI への入口

最初に `emu_status` (`GET /api/status`) を読む。98 面は
`scrn_ymax == H && grph_disp == 1`、Cirrus は
`wab_relay == 1 && wab_height == H` なら GUI。高さ H は WAB 中継中なら
`wab_height`、それ以外は `scrn_ymax`。Cirrus で残った `scrn_ymax=400` を座標に使わない。
GUI でなければ `emu_mouse` と `emu_type` はエラーになる (`off` は後始末用に通る)。

入口は CUI で rshell を閉じ、**今回の ESC によって** `[Remote shell closed]` が
画面末尾に現れたことを確かめてから `os32gui` を打つ経路だけ。
`emu_key` の `seq=ESC` の前後に `emu_tvram` を採る。重なった rshell はそれぞれ閉じる。
閉じた後、共有 `gui.key(text="os32gui")` → RETURN → `emu_status` で GUI を照合する。
既存の台本では `tools.gui_gate.begin_gui(H)` がこの確認をまとめて行う。
**rshell 中に `text=` を打たない**。rshell は分割入力を別々のコマンドとして吸う。

## 入力と起動

`emu_mouse` は `action=move|click|press|release|drag|off`、画素座標 `x,y` を受ける。
範囲は `0..639, 0..H-1`。drag の終点は `x1,y1`。絶対座標 ax/ay は渡さない。
操作が終わったら、成功・失敗にかかわらず **`emu_mouse {"action":"off"}`**。

`emu_type {"text":"abcd", "escapes":true}` は **4 文字 / 0.35s** で送る。
`\e \n \r \t \b \xNN \\` は共有の逃がし記法。GUI のみで使う。CUI は `emu_cmd`。
`seq` に `+` が入る HTTP 要求は urlencode (`CTRL+ESC` → `CTRL%2BESC`) を使う。
MCP/共有実装が符号化するので、MCP 引数では `CTRL+ESC` のまま渡す。

`emu_gui_launch {"path":"/usr/bin/gui_demo.bin"}` は CTRL+ESC → DOWN (Run の行まで)
→ RETURN → パス → RETURN。絶対 ASCII パスだけ、空白・相対パス・255B 超・引数は不可。
引数の必要な fixture は CUI 起動か fixture の対話キーを使う。
新しい kernel owner (2..5) が空から非空に変わり、OP_WAIT の `PARKED` に入るまで待つ。
v1 の返り値は `owner` と `slot:null`。**owner と SHM slot の添字を結びつけない**。
マウス経路は明示的な `fallback:true`。時間切れ後の自動再起動は二重起動になりうるので行わない。

## 観測・待ち・証拠

- `emu_gui_state`: v1 の `slots[]` は SHM 0..3、`kernel[]` は owner 1..5、
  `counters` は trim の値。窓・前景は v2 記述子があるときだけ埋まる。
- `emu_consink`: GUI 中の fixture 出力の第一の読み口。
  前回の `since` (head 位置) を渡すと差分。`lost:true` はその位置が保持されていない。
  リング 1 周やリセットを読み逃すと位置だけでは検出できない。短い区間で採取し、
  受入では drop カウンタや fixture の区間印も照合する。
- `emu_screen_text`: 第二の読み口。ゲスト ANK キャッシュの 8×16 二色字形と完全一致する
  ASCII だけを読む。日本語・未知字形は `?`、戻りは `lines[]` と `unknown_count`、`ambiguous`。
  同形字は `?` とし、`ambiguous` に行・列と候補 (例 `Il`) を返す。未知字形と別に数える。
  screen の待ちは候補のどれにも一致する。候補外の文字へ推測しない。
  候補が多い画面では、待ちの regex は長さが有限の fixture 印を使う。
  無制限の繰返し・先読み・後読み・後方参照は候補の組合せを全探索するため高価になる。
  `region:[x,y,width,height]` はセル原点を固定する。省略時は ASCII の一致を足場に行を探すため、
  ASCII が 2 字以上一致しない行は発見できない。未知字形だけの領域は region を指定する。
  `path` に PNG の証拠パスを渡す。4bit の 98 面と 24bpp の WAB を RGB に展開する。
- `emu_wait_mem {"symbol":"appslot_trim_epoch","op":"gt","value":0,"timeout":60}`。
  kernel の u32 が既定。gshell は `gshell:gshell_trim_delivered`。
- `emu_wait_text {"regex":"PASS","source":"consink","timeout":60}`。
  source は consink または screen、0.5s ポーリング。

待ちの下限は **15s**、既定 60s。時間切れは例外でなく `{"ok":false,"last":...}`。
これを受入失敗として最後の値を残す。`ok:true` は条件を読んだというだけ。
**読めた = 合格ではない** ([V1][V4])。fixture の期待、区間差分、`unknown_count`・`ambiguous` と
保存 PNG を合わせて判定する。HostDrv に置いたバイナリと実行中の版も照合する。

受入では同じ観測区間で probe の前後値、`wm_state`、consink の位置をひとまとまりに採る。
開始: probe → state → consink (since を保存)、操作、終了: probe → state → consink(since)。
個別メモリ読み取りは同時刻の原子的スナップショットではないので、前後に時刻と fixture の
区間印を付ける。証拠は `~/os32-tmp/evidence/<日付>/` に JSON・PNG として残す。

## GUI からの出口

唯一の出口は **Start → CUI mode → Yes**。ESC はアプリへ渡り、GUI の終了にはならない。
既存台本は `tools.gui_gate.leave_gshell(Mouse(H))`。戻った後、rshell が既に生きていれば
重ねて起動しない。`ver` で確認する。
この出口は `/etc/system.cfg` の **`GUI=0` を永続化**する。
再び GUI へ入るには rshell を閉じて `os32gui`。次回の GUI 自動起動へ戻すときは、
CUI の設定操作で `GUI=1` に書き戻す (既存の設定値を保つ)。

## 実際の出力例

(PM が live で採る)

`emu_status`、`emu_gui_launch`、`emu_gui_state`、`emu_consink`、`emu_screen_text`、
待ちの成功/時間切れの JSON と PNG の所在をここへ採録する。
ホスト mock の値を実ゲストの出力例として扱わない。

## Troubleshooting

| 症状 | 確認と対処 |
|---|---|
| クリックが効かない | status から H を取り直す。Cirrus は wab_height。最後に off |
| 文字が全部 `?` | セル原点、二色の fg/bg 推定、24bpp の RGB 展開、ゲストの ANK キャッシュを確認。region と PNG を保存 |
| launch が時間切れ | Start の項目順変更、rshell が text を吸った可能性、owner が消えた/OP_WAIT に入らない可能性。last と consink を読む |
| `[aidebug: timeout]` | CUI の長いコマンドがまだ動いているだけのことがある。これだけでは失敗ではない。fixture の完了印を待つ |
| 待ちが `ok:false` | GUI の条件待ちは時間切れで失敗。last を証拠にし、fixture の期待を弱めない |

ELF の既定は本体 `~/os32-v3/build/out/kernel.elf` と `~/os32-v3/userland/gshell.elf`。
`OS32_KERNEL_ELF` / `OS32_GSHELL_ELF` (または Gui の引数) で差し替える。
番地は毎回 nm -S で引く。実行中のゲストに対応する ELF を指定する。
v2 差し込み口は `gui_desc.wm_state(gui)`。記述子が無ければ v1、読取り異常は隠さない。
