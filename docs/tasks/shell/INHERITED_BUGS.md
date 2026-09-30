# 常駐シェルの継承バグ台帳 (T9 の Codex 網羅レビューで顕在化、2026-09-13)

T9 (sh.bin = 常駐シェルの CPL=3 ビルド) の実装レビュー (Codex、往復 6〜9) で、**T9 以前から常駐シェルにもあった**欠陥が
まとめて挙がった。T9 の判定は「T9 由来 + T9 の修正が持ち込んだ回帰」で行い、ここに載るものは別作業で扱う。
到達可能な反例つきのものは T9 の往復の中で直した (S7 の例外として常駐 .o が変わる)。残りは未修正。

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

## 未修正 (non-blocker、別作業)

- 注入リング満杯後に次のキーも失う (`ui.c` の UTF-8 後続待ち)。
- 複合内蔵 (`source file > out`) のリダイレクトが内側の `execute_command` で解除される。
- 255B 超の起動要求が「GUI が必要」と誤表示 (`sh_launch.inc`)。
- `ask` が ASCII しか格納しない (日本語を捨てる)。
- 入れ子 glob (`source /tmp/s*.sh` の内側で glob) の解放一覧が共有され解放漏れ / 二重 free 診断。
- 引用付き builtin (`"echo" a | echo b`) をパイプ事前判定が誤拒否。引用内の `|` も分割。`exec` は空白入り引数の引用を復元しない。
- `cp -r` の収集表 64 件上限で切る (断りは出るが、途中まで写した子は残る)。
- symlink の型が DIR 以外 FILE (`ext2_vfs.c:59`)。
- glob の `mem_alloc` 失敗が行の失敗に伝わらない (`sh_args.inc:79`)。
- `?` は照合では 1 バイトに一致するが glob の開始条件は `*` だけ。
- `sys_ls` コールバック内の KAPI 再入 (CPL=0 で int 0x80) — **カーネル側**。`find.bin` が CPL=3 で `[Process crashed]`。sh.bin は写し取りで回避 (T9 B2)。
- カーネル帯ポインタを CPL=3 に返す KAPI 5 本 (`vfs_devname` / `path_get_drive` / `path_get_cwd` / `db_column_text` / `db_last_error`) — **カーネル側**。`sys_getcwd` は T9 R1 で `ring3_user_str` の写しに。
- `losetup` 後の退場で loop スロットが回収済み FD を保持 (`drivers/loop_dev.c:928`) — **カーネル側**。sh.bin では `losetup` / `dd loN` を cui only に。
- `source` の 255B 超の行が切断されて実行される (`cmd_script.c:121`、`env_expand` の検知より前)。
- ファイル名補完が 126B で名前を切る (`ui.c:210`)。
- `cat -n` が読み込み区切りを行末として扱い余分な番号付き空行を出す (`cmd_file.c:353`)。
- シェルの `ls` (`userland/shell/cmd_dir.c:43`) は `sys_ls` の戻り値を見ないので、列挙途中の I/O 失敗 (S3I2-K で FAT が負を返すようになった後も) を表示できない — **シェル側** (2026-09-14、TASK_S3I2 §4-7)。


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
