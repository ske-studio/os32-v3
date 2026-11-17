# userland/shell/sh_redraw.inc — 設計 (sh.bin の入力行の再描画)

> 状態: **現行 (2026-11-18)** — 出所: TASK_T9_sh (§1 D2(d)) の契約を今のソースと照らして要約
ソース: `userland/shell/sh_redraw.inc` (`redraw_line`・`sh_erase_cells`。`SHELL_AS_APP` の `ui.c` が取り込む)

GUI 端末の中の `sh.bin` は、入力行の再描画にコンソールの座標 (`console_get_cursor_*`・`console_set_cursor`) を使わない。GUI 中はコンソールが座標を進めず、端末も範囲外の CURSOR を黙って捨てるので、座標に頼ると入力位置が戻らず別の行を上書きする。印字するバイト列 (PRINT) だけで組み立てる。

## 規則 (`redraw_line(buf, len, cursor)`)

画面に出ている行の写し (`sh_drawn`・`sh_drawn_len`・画面カーソルのバイト位置 `sh_drawn_pos`) と比べる。

| 場合 | 出すもの |
|---|---|
| 延長: 画面カーソルが行末・新しい行が写しの前方一致で同じか長い・新しいカーソルも行末 (TAB 補完の多く) | 増えたバイトだけ。改行しない。変化が無ければ何も出さない |
| それ以外 (履歴・BS・候補一覧の後・縮んだ行・カーソルが行末より前) | `\n` + プロンプト + 行全体 |
| 新しいカーソルが行末より前 | 続けて、カーソルから行末までの字数ぶん BS (0x08)。UTF-8 の後続バイトは数えない |

- どの場合も座標の KAPI を 1 度も呼ばない。`\r` は行頭へ戻るだけで消さないので、作り直しは改行で下りる。
- 写しが壊れている印 (`sh_drawn_len < 0`、候補一覧を挟んだ等) のときは必ず作り直す。

## 消す

端末の BS は 1 セル左へ動くだけで消さない。行末の 1 字を消すときは BS + 空白 + BS で上書きする (全角は 2 セル: 最初の BS は 1 回で全角の先頭へ戻り、空白 2 個、BS 2 個)。`sh_erase_cells(removed)` は消したバイトの先頭が 3 バイト列 (`0xE0` 台) なら 2 セル、それ以外は 1 セルを消す (`cmd_script.c` の `ask` も使う)。

## 検査

`make check-sh-exec-host` の `tools/tests/test_sh_shell.py` (`sh_shell_host.c`)。`sh_redraw.inc` と `cmd_script.c` をそのまま取り込み、出力の KAPI と座標の KAPI の呼び出しを数える。同じ試験の `source` 中の `exit` (次の行・`goto` のループ・ネストした `source` の外側も走らない) は `docs/design/userland/shell/exit_status.md` の「4. `exit [N]` と終了要求」の契約。
