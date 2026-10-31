# userland/shell/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN・GF_STAGES_DESIGN・REALHW_RECOVER_ADDENDUM・REALHW_RECOVER_DESIGN・RECOVER_ROUTE_TEST・RECOVER_T4B_INJECT・API_CONTRACTS (D1 で移した)
ソース: `userland/shell/`

### cmd_base

- cmd_ver は KAPI を一回呼び、kind の低8bitに応じて既存色を選んで `kprintf(attr, "%s", text)` で出力。

CUI はその bit に従って先頭2空白、各行末に改行を追加。見出し・kdebug・sqlite stack は字下げなし。

- shell_print_kdebug の詳細コマンドは維持するが、ver からは呼ばない。ver/About の情報源は新経路だけ。

### cmd_recover

`recover restore`（追加引数なし）を `--restore` から送る

- shell共有opは3=status・4=now

`status / now <session>` の引数と結果。既存 kdebug_call 経由。

#### 2 口の形と寿命

`recover route-test 0 --approve`。承認なしの形はusage拒否、ポート値の引数は設けない。

- KDEBUG=0は既存NOSYSのまま、I/O 0件。shellは既存エラー表示を使い、結果はrec部分から従来形式で表示する。

- shell の専用形の session は文字列 `0` のみを受け付ける。

shellはstatus/now（op 3/4）のみ

> [要確認] 「status/now（op 3/4）のみ」に対し、現行の `userland/shell/cmd_recover.c` は `restore`・`route-test`・`virtual` も受け付ける。

`recover virtual begin <case>` / `recover virtual step <session>` / `recover virtual end <session>` を追加。case は固定列挙、一命令を承認として扱う。

### cmd_sys

- CUI の `os32gui` (引数なし) = `sys_switch_shell("/bin/gshell.bin")` + exit。
  `os32gui on|off` = `system.cfg` の書き換えだけ (次回起動から)。

## 索引

- [cmd_gfinit.c](cmd_gfinit.md)
