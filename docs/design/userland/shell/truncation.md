# userland/shell の入力の上限と断り — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SH_TRUNCATION の契約を今のソースと照らして要約
ソース: `userland/shell/main.c`・`userland/shell/shell.h`・`userland/shell/sh_exec.inc`・`userland/shell/sh_args.inc`・`userland/shell/sh_launch.inc`・`userland/shell/cmd_script.c`・`userland/shell/cmd_env.c`・`userland/shell/cmd_base.c`・`userland/shell/cmd_mnt.c`・`userland/shell/cmd_sys.c`・`userland/shell/cmd_filer.c`・`userland/shell/rshell.c`・`userland/shell/rshell_lan.c`・`userland/shell/ui.c`

試験は `sh_truncation_host.c (旧版 e16688ff4188)` (登録表と `execute_command` を実物で通す)。
利用者に見える形の一部は `docs/manpages/source.1` の LIMITS。

## 1. 規則 — 切り詰めたら実行しない

入力が固定長の上限に収まらないと分かったら、**その場で赤字 1 行を出し、行全体を実行しない**。
切り詰めた値で比較・起動・登録・展開へ進まない。上限そのものは広げない (定数は `shell.h` ほか)。

- 断る側は `sh_refuse(what, limit)` を呼ぶ。表示は `<what> too long (max <limit>)`、同時に印
  `sh_refused_flag` を立てる。読み切れなかった (上限の書式に乗らない) 場合は自分で 1 行出して `sh_refuse_mark()`。
- 断った行の `$?` は 2 (`SH_STATUS_USAGE`)。値の割り当ては `docs/design/userland/shell/exit_status.md`。
- 上限の境界は「以上」で断る。例: `if` の値はクォート除去後 255 文字は通り、256 文字は断る。

## 2. 印の寿命 (`shell.h` の注記が正)

- 消すのは**いちばん外側の** `execute_command` の入口だけ (`g_exec_depth == 0`)。`if` / `time` が組み立てた
  行の入れ子の呼び出しでは消さない (消すと内側の断りが外へ届かない)。
- `script_exec` は 1 行ごとに `sh_refused_take()` で読んで消し、立っていたら `script_abort_flag` を立てて
  スクリプトを打ち切る (`script: aborted (line refused)`)。断った行だけ飛ばして次へ進むと、
  `if … goto` で飛び越されるはずの後続行が走るため。
- 入れ子の `source` は `script_source_file` の戻り値 `SCRIPT_ERR_REFUSED` (-2) で親へ伝え、`cmd_source` が印を立て直して親も打ち切る。
- 起動時の profile (`script_source_profile`) は断られても起動を止めない: 印を下ろし、
  `sh: <path> aborted; continuing with defaults` を出して続行する。
- パイプの段ループは `sh_refused_peek()` で**読むだけ**。印が立った段があれば後続の段を実行せずに行を終える
  (断った段の後ろの `tee` やリダイレクトの書き込みを起こさない)。
- 対話の行編集と `rshell` は断った後すぐ印を下ろす (次の行は普通に動く)。
- PATH の走査中に印が立ったら次の候補へ回さない。

## 3. 断る経路と上限

| 経路 | 上限 (定数) | 断ったとき |
|---|---|---|
| `if` の左右の値 (`cmd_if`) | `IF_VALUE_MAX` (256、クォート除去後) | 比較もコマンドも実行しない |
| `if` が組み立てるコマンド行 | `CMD_BUF_SIZE` | 実行しない |
| スクリプトの 1 行 / 行数 / ファイルの読み込み (`script_load`) | `SCRIPT_MAX_LINE` (256) / `SCRIPT_MAX_LINES` (128) / 読み切れない | 1 行も実行しない |
| 外部コマンドの再構築 (`try_exec`)、プログラムのパス | `TRY_EXEC_BUF_SIZE` (512)。クォートの再付与で伸びる分も数える | 子を起こさない |
| `exec` 組み込み / `time` のコマンド行 | `EXEC_CMDLINE_MAX` (256) / `TIME_CMD_MAX` (512) | 子を起こさない |
| コマンド名に `.bin` を付ける (`run_cmd_internal`) | `PATH_MAX_LEN - 5` | 別のファイルを起動しない |
| PATH の項目 / 項目とコマンドの連結 (`try_exec_from_path`) | `PATH_MAX_LEN` | 区切りを見失って別のディレクトリを試さない |
| パイプの段数 / 空の段 / 1 段の長さ (`split_pipeline`) | `MAX_PIPE_STAGES` (8) | 行全体を実行しない。空の段 (`echo ok \|`・`\| echo`・`a \|\| b`) も断る。分割はクォートを見ないので `echo "a\|\|b"` も断る |
| glob のパターン / ディレクトリ部 (`sh_args.inc`) | `GLOB_PATTERN_MAX` (256) / `PATH_MAX_LEN` | 一致を試みない |
| glob の一致の確保失敗 (`glob_cb`) | — | 行全体を断り handler を呼ばない。確保済みは解放する |
| コマンド行 / 展開後の行 / 変数名の展開 (`execute_command_line`・`env_expand`) | `CMD_BUF_SIZE` / `ENV_NAME_MAX` | 長い行を空行と区別して断る。`$NAME`・`${NAME}` の名前が 32 文字以上なら展開エラー |
| `set` / `export` / `ask` の名前・値 | `ENV_NAME_MAX` (32) / `ENV_VALUE_MAX` (256)、`ask` の prompt・入力 | 登録しない |
| `sh.bin` の要求表へ送る行 (`sh_launch.inc`) | `LAUNCH_CMDLINE_MAX` (256) | `launch_req` を呼ばない (「GUI 外」と読み違えない) |
| 行編集の打鍵 (`ui.c`) | `CMD_BUF_SIZE - 4` | 実行せず履歴にも入れない (`sh: line too long`) |
| 履歴 (`hist_add` / `hist_load`) | `HIST_LINE_MAX` (512) | 収まらない行・途中で切れた行は履歴に入れない / 読み込まない |
| `HOME` から組む履歴・`.profile` のパス | `PATH_MAX_LEN` | 別ディレクトリを読み書きしない |
| `rshell` の受信行 (`rshell.c`・`rshell_lan.c`) | `RSHELL_LINE_MAX` (128) | 行末まで読み捨て、実行しない。**EOT (0x04) は必ず返す** (`/api/cmd` は EOT を待つので、返さないと以後が全滅する) |
| `rshell` の `push` / `recv` のホスト側パス | `host_path` の大きさ | 切れた別パスを作らない |
| `/etc/system.cfg` の書き換え (`cmd_sys.c`) | 読み込み・書き戻しの大きさ | 切れたまま書き戻さない |
| `filer` の名前・パスの連結 (`cmd_filer.c`) | `FL_MAX_NAME_LEN` (64) ほか | 収まらない名前は表に載せず数える (`fl_state.dropped`)。連結が溢れたら `Path too long` / `Command line too long` で実行しない |
| `filer` の関連付け表 (`ft_load`) | `FL_FILETYPES_MAXSZ` | 読み切れなければ関連付け表を使わない (切れた末尾行で別のコマンドに関連付けない) |
