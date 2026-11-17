# userland/cmds/cfg.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S2・TASK_S5 の契約を今のソースと照らして要約
ソース: `userland/cmds/cfg.c`

設定レジストリの CUI (`cfg.bin`)。`libos32cfg` を静的リンクし、KAPI だけを使うので GUI の端末でも動く。
library の約束は `docs/design/userland/lib/cfg/libos32cfg.md`、`init` の中身は `docs/design/userland/lib/cfg/cfg_init.md`。

```
cfg get <scope> <key> [default]
cfg set <scope> <key> int|text|blob <value>
cfg del <scope> <key>
cfg list [<scope> [<prefix>]]
cfg status
cfg init [--tsv <path>]
cfg export <file>
cfg import ...
```

## 出力と直列化

- コンソールへの出力は DB を **閉じた後** に、1 KB (`CFG_OUT_CHUNK`) ごとに `sys_yield` を挟んで書く。open から close の間には yield しない。
- `list` は溜め場 (`CFG_OUT_MAX`) が一杯になったら DB を閉じて吐き出し、開き直して最後に出した `(scope, key)` の続きから出す。件数の上限を作らずに、yield の約束を保つ。
- `list` の 1 行は `<scope>\t<key>\t<type>\t<value>`。blob は `blob:<長さ>` だけ (hex は `get` で出す)、値が NULL の行は `(unset)`。
- 値の取得に失敗した行は 0 や空で埋めず、`list failed (<scope> <key>)` で止める。scope の引数が 63 byte を超えたら切り詰めずに `bad scope`。

## 副指令ごとの約束

| 副指令 | 出力と終了コード |
|---|---|
| `status` | `<状態名>` に、OK・VERSION なら ` schema_version <n>`、ERROR・CORRUPT なら ` sqlite=<code>`、どの状態でも末尾に ` pool <n> B` (下)。終了コードは `CFG_OK` のときだけ 0 |
| `get` | 型を `cfg_get_info` の完全一致 1 行照会で引いてから値を 1 回取る (列挙は使わない)。int は 10 進、text はそのまま、blob は hex。未設定 (行が無い・値が NULL) は `default` か `(not set)` で終了 0。障害・契約外の値は `get failed` で 1 |
| `set`・`del` | 状態が `CFG_OK` でなければ `cannot write: <状態名>` で 1。値の解釈の失敗、`begin`・`set`・`commit` の失敗も 1 |
| `list` | MISSING は何も出さず 0。CORRUPT・ERROR は状態名を出して 1。VERSION は読める |
| `init` | 成功は `cfg: created /etc/settings.db`。失敗は `cfg_last_init_reason` ごとの文言で 1 |
| `export` | 下 |

どの副指令も `cfg_close` が失敗したら `close failed (<code>)` を出して 1。

## `status` の pool

` pool <n> B` は KAPI `db_mem_used` (SQLite の MEMSYS5 プール **全体** の使用量、FEP の辞書など他の常駐接続の分を含む) を、接続を持っている間に読んだ値。状態によらず付ける。u32 を符号なしで出す (`fmt_u32`。`fmt_int` に通すと 2 GB を超える値が負になる)。

## `export`

- 出力先を正規化し、設定 DB・journal・`.new*` を指していたら DB を開く前に拒否する (`PROTECTED_PATHS`)。
- 1 行目はヘッダ `{"schema_version":<実値>,"exported":"<tick>"}`。版は実値 (VERSION の DB なら認識版より大きい値のまま)。2 行目からは 1 行 1 レコード `{"scope":…,"key":…,"type":<n>,"v":…}` (int は数、text は JSON 文字列、blob は base64、値が NULL なら `null`)。行数 = 件数 + 1。
- `list` と同じ再開の仕組みで、溜め場が一杯になったら DB を閉じて出力ファイルへ流す。標準出力へは要約 1 行だけ。
- MISSING・CORRUPT・ERROR は `cannot export: <状態名>` で 1。途中で落ちたら出力ファイルを消す。

## 検査

`tools/tests/test_cfg.py` の `args` (引数解釈と整形の純関数 `cfg_cmd_parse`・`fmt_int`・`parse_int`・`fmt_hex`・`parse_hex`・`fmt_b64`・`fmt_json_str`・`status_name`・`type_name`)、`cmd` (`status`・`init`・`list`・`set`・`get`・`del`・`export` の通し)、`list_big` (255 byte の text 100 件を重複なく出し、`sys_yield` が呼ばれる)、`s5_pool` (`pool` の表示と `fmt_u32`)。`cfg.c` は `main` を `cfg_main` に差し替えて丸ごと組む。
