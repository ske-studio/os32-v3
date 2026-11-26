# userland/shell の終了コードと `$?` — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_EXIT_STATUS の契約を今のソースと照らして要約
ソース: `userland/shell/main.c`・`userland/shell/sh_exec.inc`・`userland/shell/sh_launch.inc`・`userland/shell/cmd_env.c`・`userland/shell/cmd_script.c`・`userland/shell/cmd_base.c`・`userland/shell/shell.h`

利用者に見える形 (`exit` の引数・`source` の値・`set -e` の表示) は `docs/manpages/exit.1`・`docs/manpages/source.1`、
カーネル側の記録 (`exec_last_result` の種別・記録なしの扱い・GUI の子は書かない) は `docs/KAPI_SPEC.md` の
`exec_last_result` の項が正典。ここはシェルの中の配線を書く。試験は `sh_status_host.c (旧版 cd26a2e50c02)`。

## 1. 値の空間

`$?` の値は `shell.h` の `SH_STATUS_*` に 1 か所でまとめてある。

| 定数 | 値 | 意味 |
|---|---|---|
| `SH_STATUS_OK` | 0 | 成功 |
| `SH_STATUS_ERROR` | 1 | 実行はしたが失敗した (組み込みの一般の失敗) |
| `SH_STATUS_USAGE` | 2 | 実行しなかった (構文・引数・断り) |
| `SH_STATUS_NOEXEC` | 126 | 見つかったが起こせなかった |
| `SH_STATUS_NOTFOUND` | 127 | 見つからなかった |
| `SH_STATUS_ABORTED` | 130 | CTRL+STOP / ESC で畳んだ |
| `SH_STATUS_FAULT` | 139 | 例外で畳んだ |

`$?` (`sh_status_get` / `sh_status_set`) の初期値は 0。環境変数表には書かない (子に継承させない)。
「断った印」(`sh_refused_flag`) とは役割が別で、印は実行を拒否したかの制御信号、`$?` はその結果の値。

## 2. 外部プログラムの結果 — 種別で分ける

起動の口は `sh_exec_result(cmdline, &kind, &code)` の 1 つ。`try_exec` / `try_exec_from_path` と
`exec` 組み込みはこれだけを呼ぶ。

- 常駐 (`SHELL_AS_APP` なし、`sh_exec.inc`): `exec_run` の**直後に** `exec_last_result` を読む (間に KAPI を挟まない)。
  記録が無ければ `EXEC_KIND_GENERAL` に倒す (= 次の候補へ進まない)。
- `sh.bin` (`SHELL_AS_APP`、`sh_launch.inc`): **カーネルの記録を読まない** (自分の子とは限らない)。
  要求表の状態を写す: `LAUNCH_ST_DONE` → `(EXEC_KIND_EXITED, 0)`、起動できなかった・起こす前に断った
  (GUI 外・パイプ・リダイレクト・長さ) → `EXEC_KIND_NOMEM`。終了コードは運べないので `sh.bin` の `$?` は
  「起動できたか」までしか表さない。

`sh_status_from_kind` (`main.c`) の写像:

| kind | `$?` |
|---|---|
| `EXEC_KIND_EXITED` | `code & 0xFF` (`exit(-1)` = 255、`exit(-2)` = 254 …) |
| `EXEC_KIND_FAULT` | 139 |
| `EXEC_KIND_ABORTED` | 130 |
| `EXEC_KIND_NOT_FOUND` | 127 |
| `EXEC_KIND_INVALID` | 126 |
| それ以外 (`NOMEM`・`GENERAL` …) | 126 |

**値から種別を推測しない** — `exit(-2)` は fault ではない。fault のときの表示 (`[Process crashed]`) は変えない。

### PATH の走査 (`run_cmd_internal`、`sh_exec.inc`)

- 候補は cwd → PATH の各項目の順。次の候補へ進むのは `NOT_FOUND` と `INVALID` のときだけ。
  `EXITED` / `FAULT` / `ABORTED` / `NOMEM` / `GENERAL` は**どんな値でも止まる**
  (子が `exit(-1)` を返しても「無かった」と読まない = 同じプログラムを 2 回起こさない)。
- 走査中に `INVALID` を 1 度でも見たかを cwd と PATH をまたいで覚える。尽きたとき、見ていれば
  `not a valid executable` で 126、全部 `NOT_FOUND` なら `command not found` で 127。
- 候補を試す途中で断りの印が立ったら、その場で 2 を返し、次の候補へ回さない。

## 3. handler に届かない行・組み込みの値

| 行 | `$?` |
|---|---|
| リダイレクト失敗・長すぎる行・引数過多・パイプの確保失敗・断った行 | 2 |
| `command not found` | 127 |
| 空行・空白だけの行 | **変えない** (直前の値のまま) |
| リダイレクトだけの行 (`> /tmp/out`) / `-h` の短絡 | 0 |

組み込みの handler は `int (*)(int, char **)` (`CmdHandler`)。赤字のエラーを出す分岐は非 0。

- `source` は最後に実行した行の値。実行する行が 0 本 (空・コメントだけ) なら 0。開けない・読めない・
  深すぎる・行を断って打ち切ったら 2 (`script_source_file` の負の戻り値をまとめて写す)。ESC で中断したら 130。
- `time` は内側の値。`if` は真なら内側の値、**偽なら 0** (前の値を持ち越さない)。
- `goto` のラベル無しは非 0 で打ち切る。
- パイプラインは**最後の段の値**。ただし途中の段で `exit` が立てば後段を実行せず、その値になる。
- パイプ行の `$?` は段を回す前に 1 回だけ展開される。

## 4. `exit [N]` と終了要求

- 引数は `sh_exit_arg` が読む: 省略は直前の `$?`、0〜255 の 10 進だけ有効。非数値・255 超・2 つ以上は
  実行せず 2 (要求も立てない)。
- 「要求が立ったか」(`sh_exit_flag`) と「値」(`sh_exit_code`) は別の変数 (`exit 0` でも要求が立つ)。
  handler は**値を書いてから要求を立てる**。パイプの段ループと `script_exec` が要求を見て打ち切り、
  その値を行・スクリプトの値にする。
- 登録はビルドで分ける (表は先勝ち): 常駐は `cmd_script.c` の `exit` (対話ではシェルを終わらせず `$?` を N にする、
  スクリプト中は打ち切る)、`sh.bin` は `cmd_base.c` の `exit` (端末を閉じる)。常駐はいちばん外側の
  `execute_command` が 1 行ごとに `sh_exit_flag` を下ろす。

## 5. `$?` の展開 (`env_expand`、`cmd_env.c`)

- `$` の直後・`{` の判定と名前の走査より**手前**で `?` を特別扱いする。だから `set ?=5` で作った変数があっても隠れる。
- `$?x` は `<値>x` (`0x`・`12x`)。`$` 単独はそのまま。**`${?}` は対応しない** (空に展開される)。
- 区切りを挟めば `$VAR` と併用できる (`$VV:$?`)。既存の `$VAR` の展開は変えない。

## 6. `set -e` / `set +e`

- `cmd_set` の**先頭**で `-e` / `+e` を拾い、`cmd_script.c` の `script_errexit` を `script_errexit_set` で立て下げする
  (`set` の登録は 1 本)。
- 立っていると `script_exec` は値が 0 でない行で打ち切り、`script: line N: status S` を 1 行出す。
  N は保持された行の位置 (1 起点)。コメントと空行は読み込みで詰められ、ラベル行は残る。
  外部の失敗・組み込みの失敗・リダイレクト失敗・未知コマンドのどれでも止まる。
- 入れ子の `source` を抜けるときに旗を退避・復元する。

## 7. 起動時の profile

`script_source_profile` (`/etc/profile`・`$HOME/.profile`) は、実行の後に `$?` を 0 に戻す
(profile の中の失敗を最初のプロンプトの `$?` に残さない)。断られた profile でも起動は止めず、断りの印も下ろす。
