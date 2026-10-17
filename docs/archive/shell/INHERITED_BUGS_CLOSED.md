# 常駐シェル継承バグの閉鎖・受入記録

> 状態: **完了記録**

[未完了台帳](../../tasks/shell/INHERITED_BUGS.md) から修正済み行と受入記録を抽出。日付付きの記述は当時の記録。

## T9 の往復で修正済み (常駐にも効く別コミット)

| ID | 内容 | コミット |
|---|---|---|
| R6 | 別 FS への `mv` がコピー失敗後も原本を削除 (`do_copy_file` が 0 を返していた) | `20ea335` |
| R7 | 256 引数で `argv[256]` の配列外書き込み | `940b5d6` |
| I1 | 引数上限で切り捨てて `cp` の宛先が変わる → 行を捨てる | `6aed6d2` |
| I2 | HostDrv (`st_ino` 0) で `cp /host/a /host/./a` が原本を切り詰める → パス正規化 | `6aed6d2` |
| I3 | `dd cd0` が 1024B 確保で 2048B 受ける | `6aed6d2` |
| I4 | `wildcard_match` の指数的バックトラック → 反復型 (往復 9 で C-1 回帰、修正中) | `6aed6d2` |
| I-1〜I-6 | `ide` の `IdeInfo` 不一致 / `env_expand` の打ち切り / `fs_join_path` の切り詰め / `cp -r` の 31B 名 / `dd hd0` のセクタ長 / `dd` の write 戻り値 | `ecaba37` |
| C-1 | I4 の反復型 `wildcard_match` が `*` を含む名前を落とす回帰 | `0c44364` |

## 2026-09-16 に修正 (常駐シェル、RED → GREEN 付き)

| 内容 | 直したもの | 記録 |
|---|---|---|
| 内蔵 `cat` が stdin を読まない (`echo a \| cat` / `cat < file` が空) | `cmd_file.c` の `cmd_cat` — 引数が 1 つも無ければ FD 0 を読む (`cat_stream` へ切り出し)。FD 0 は閉じない。端末のままなら読みに行かない (`vfs_read_fd` の TTY 経路に EOF が無いため) | [`tools/tests/cat_linenum_tdd.md`](../../../tools/tests/cat_linenum_tdd.md) §7 |
| `cp -r` が失敗時に宛先へ空のディレクトリを残す | `cmd_file.c` の `do_copy_recursive_impl` — 収集して件数を確かめてから `sys_mkdir`、戻り値を見る。既に在る**ディレクトリ**への上書きコピー (`OS32_ERR_EXIST` + 型が DIR) だけは従来どおり通す | [`tools/tests/fs_kind_callers_tdd.md`](../../../tools/tests/fs_kind_callers_tdd.md) §6 |
| `source` が入力の先頭を捨てる (`cmd_script.c` の行ごとの ESC 監視が ESC 以外も消費) | **KAPI v54** の `kbd_peekkey` (覗くだけ = キューを動かさない) を足し、`script_exec` の監視を `kbd_trygetkey` から差し替え。取り除くのは ESC と分かってから 1 回だけ。`drivers/kbd.c` + `drivers/serial.c` (`serial_peekchar`) + `kernel/kbd_inject.c` (`kbd_inject_peek`) | [`tools/tests/sh_truncation_tdd.md`](../../../tools/tests/sh_truncation_tdd.md) §5 |

## 棚卸しで閉鎖 (2026-10-17、現行コード確認)

| 内容 | 修正の根拠 |
|---|---|
| `source` の 255B 超の行が切断されて実行される | [cmd_script.c](../../../userland/shell/cmd_script.c) の `script_load()` は上限超過を拒否し、読み込んだスクリプト全体を破棄して実行を止める。試験: [sh_truncation_tdd.md](../../../tools/tests/sh_truncation_tdd.md) |
| `cat -n` が読取り区切りに余分な行番号を出す | [cmd_file.c](../../../userland/shell/cmd_file.c) の `cat_stream()` が `at_bol` を読取り間で保持し、`cat_with_linenum()` は行頭でのみ番号を付ける。試験: [cat_linenum_tdd.md](../../../tools/tests/cat_linenum_tdd.md) |

## ゲスト受入 (PM、2026-09-16、`bae54cb`)

| 反例 | 結果 |
|---|---|
| `echo hello \| cat` | `hello` (パイプから読む) |
| `cat < /tmp/r.txt` | `redir` (リダイレクトから読む) |
| `cat -n < /tmp/r.txt` | `     1  redir` (行番号 1 つ、余分な行なし) |
| `cp -r` の 1 回目 | 成功 |
| `cp -r` の 2 回目 (**既存ディレクトリへの上書き**) | 成功。中身が新しい内容に更新され、入れ子のファイルも保たれる |

コーダーが「実機で最初に見るべき点」と挙げた「贋 FS の `OS32_ERR_EXIST` の約束が実物で成り立つか」は
**成り立つ**ことを確認した。
