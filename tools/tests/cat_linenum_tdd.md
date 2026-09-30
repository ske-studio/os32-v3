# `cat -n` の行番号は行の先頭でだけ出る (ホスト試験の記録)

- 対象: [`userland/shell/cmd_file.c`](../../userland/shell/cmd_file.c) の
  `cat_with_linenum` / `cat_stream` / `cmd_cat` (`cat` `cat2` の 2 名で同じ実装)
- 実行: `python3 -B tools/tests/test_cat_linenum.py [--target] [--mutate]`
  (`make check-cat-linenum-host` が同じものを `--target --mutate` 付きで回す)
- 試験: [`cat_linenum_host.c`](cat_linenum_host.c) — 実物の `cmd_fs_shared.c` と
  `cmd_file.c` を 1 行も写さずそのまま `#include` し、KernelAPI だけを贋物にする
- 原則: [`docs/POLICY_DEBUG.md`](../../docs/POLICY_DEBUG.md) §4-32 —
  FS の read が要求長ちょうどを返すと思い込まない (ここでは **1 回の読み取りで
  ファイル全部が来る**と思い込んでいた側の同じ間違い)

## 1. 何を確かめる試験か

`cat -n` には欠陥が 2 つあった。どちらも「行番号を**行の終わりで**出していた」
ことから来る。

### (a) 末尾に余分な行番号が出る

ループが `for (i = 0; i <= len; i++)` で回り、`i == len` のときにも必ず行番号を
出していた。**改行で終わるファイル**では最後の改行の後ろに中身のない行がもう
1 行出る。

```
$ printf 'a\nb\n' > f ; cat -n f
     1  a
     2  b
     3          <- これが出ていた (正しくは 2 行)
```

### (b) 読み取りの切れ目ごとに余分な行番号が出る

`cmd_cat` は `sys_read` 1 回ごとに `cat_with_linenum` を呼ぶ。行頭かどうかの
状態を読み取りをまたいで持たないので、**ファイルが `IO_BUF_SIZE` (65536) を
超えると切れ目ごとに (a) と同じことが起きる**。行の途中で切れても、そこで 1 行
終わったものとして扱われ、番号が 1 つ余分に増えて 1 行が 2 行に割れる。

### 正しい挙動 (POSIX の `cat -n`)

- 行番号は**行の先頭で**出す。改行を見たら次の行の先頭で出す。
- 最後が改行で終わるなら、そこで終わり。余分な行番号を出さない。
- 最後が改行で終わらないなら、その最後の行にも行番号を出して中身を出す。
- 空ファイルは 1 行も出さない。
- 書式は 6 桁右寄せ + 空白 2 つ (変えない)。

## 2. どう判定するか

判定は文言の照合ではなく、**`sys_write(1, ...)` に出た全バイト**を、試験が別に
書いた素朴な参照実装 `ref_cat_n` (`sprintf("%6d  ")`) と 1 バイトずつ突き合わせる。
書式 (6 桁右寄せ + 空白 2 つ) もこの照合で固定される。

(b) は 2 通りで踏む。

| 踏み方 | 何を作るか |
|---|---|
| `catf_chunk` | `sys_read` が要求より短く返す (1/2/3/4/5/7 バイト刻み)。実 FS と同じ短い読み取り |
| 大きいファイル | `IO_BUF_SIZE` ちょうど / 境界で行が切れる / 境界が改行と重なる / 境界の直後が改行 / 3 回読む長さ |

`-n` 無しの `cat` が 1 バイトも変えずに素通しすることも同じ試験で見る
(`2*IO_BUF_SIZE+33` バイト、NUL を含む生バイト入り)。

## 3. RED → GREEN (2026-09-16)

RED (直す前の `cmd_file.c`):

```
=== 53 件中 38 件 FAIL ===
EXIT cat_linenum_host=1
```

```
  FAIL 'a\nb\n': 行番号は 2 つ (3 つ目を出さない)
       行番号の個数 出た=3 期待=2
       出た   [     1  a|     2  b|     3  |]
       期待   [     1  a|     2  b|]
  FAIL 境界で行の途中が切れる (改行は境界の先)
       行番号の個数 出た=4 期待=2
  FAIL 読み取り 3 バイト刻み: 本文 1 番
       出た   [     1  one|     2  |     3  tw|     4  o|     5  t|     6  hre|     7  e|     8  |]
       期待   [     1  one|     2  two|     3  three|]
```

GREEN (直した後):

```
=== 53 件中 0 件 FAIL ===
EXIT cat_linenum_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_fs_shared.c)
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_file.c)
```

## 4. 直し方

`cat_with_linenum` に `int *at_bol` (「次に出す 1 バイトが行の先頭か」) を足し、
`cmd_cat` がファイル 1 本のあいだ `line_num` と並べて持ち回る。ループは
`while (i < len)` で回り、

1. `*at_bol` が立っていれば行番号を出して下ろす
2. 改行までを一気に書く (改行も行の一部として書く)
3. 改行を見たら `*at_bol` を立てて `*line_num` を進める

改行を見ないままバッファが尽きたときは `*at_bol` が下りたままになるので、次の
読み取りは行の途中から続く。

**最後が改行で終わらないファイルの行末**: 従来どおり改行を 1 つ足す。足す場所は
`cat_with_linenum` の中ではなく `cmd_cat` の読み取りループの後 (`!at_bol` のとき)
へ移した。読み取りの途中では「ここで行が終わった」と決められないため。
POSIX の `cat -n` は足さないが、シェルのプロンプトが行頭から出るよう従来の挙動を
保つほうを採った (変えるなら別票で)。

## 5. 否定側 (`--mutate`)

`cmd_file.c` を一時的に書き換えて、この試験が RED になることを見る。

| 変異 | 戻すもの | 落ちる件数 | 落ちる主な場 |
|---|---|---|---|
| `a_extra_number_at_end` | (a) バッファの終わりでも 1 行終わったことにする | 38 | `'a\nb\n'` `'\n'` `'\n\n\n'` / 境界が改行と重なる / 末尾が改行 |
| `b_state_not_carried` | (b) 読み取りごとに行頭の状態を捨てる | 32 | 1〜7 バイト刻みの読み取り / 境界で行の途中が切れる / 3 回読む長さ |

(件数は §7 の 36 件を足した後の値。§7 を入れる前は 32 / 28 だった。)

2 つの変異が落とす場は重ならない側を持っている ((a) は改行で終わる場、(b) は
行の途中で切れる場)。どちらか片方だけ直しても GREEN にならない。

## 6. 確かめていないこと ([V4])

- **実機 (NP21/W) では回していない**。ホストの gcc と i386-elf のコンパイルまで。
- `cat -n f1 f2` の**ファイルをまたいだ行番号**は触っていない。`cmd_cat` は
  今もファイルごとに 1 から数え直す (GNU の `cat -n` は通し番号)。この票の
  範囲外として残した。
- `cat -n` のオプション解析 (`-n` 以外の文字や `-` 単独) も触っていない。

## 7. 追補 — 引数なしの `cat` が標準入力を読む (2026-09-16)

継承バグ台帳 ([`docs/tasks/shell/INHERITED_BUGS.md`](../../docs/tasks/shell/INHERITED_BUGS.md))
の「内蔵 `cat` は stdin を読まない (`echo a | cat` / `cat < file` は空)」を同じ枠で直した。

### 7-1 欠陥

`cmd_cat` は `for (i = file_start; i < argc; i++)` でファイル名の引数だけを回す。
引数が 1 つも無いと**ループが 1 回も回らず、何も出さずに戻る**。
シェルは `fd_redirect` で FD 0 をパイプ用の buffer / リダイレクト先のファイルへ
差し替えているので、読む先は用意されているのに誰も読んでいなかった。

### 7-2 直し方

読み取りループを `cat_stream(int fd, int show_linenum)` へ切り出し
(`line_num` / `at_bol` の持ち回りと行末の改行は §4 の規則のまま、書式も不変)、
`cmd_cat` は

- ファイル名が 1 つも無い (`file_start >= argc`) → `cat_stream(0, show_linenum)`
- ある → 従来どおり 1 本ずつ `sys_open` して `cat_stream(fd, ...)`

と振り分ける。**リダイレクト表には触らない** — `sys_read(0, ...)` が
`vfs_read_fd` → `fd_redirect_read(0, ...)` へ落ちる既存の仕組みにそのまま乗るので、
パイプ (`echo a | cat`) でもリダイレクト (`cat < f`) でも同じ経路で効く。

2 つの決まりごと:

- **FD 0 を `sys_close` しない。** FD 0 はシェルの持ち物で、閉じると以降の
  リダイレクトが壊れる。`cat_stream` は FD を閉じず、閉じるのは開いた側だけ。
- **`sys_isatty(0)` が真なら読みに行かない。** `fs/vfs_fd.c` の `vfs_read_fd` は
  リダイレクトされていない FD 0 を `kbd_getchar()` で読み、**EOF を返す手が無い**。
  端末のまま読むと `cat` だけを打ったユーザーが戻れなくなるので、
  `grep` / `hexdump` (`userland/cmds/`) と同じく使い方を出して止める。
  `vfs_isatty` はリダイレクト中 (パイプを含む) は 0 を返すので、直したい経路は塞がない。

引数に `-` を混ぜる形 (`cat a - b`) は**対象外**。今の解析は `argv[1]` が `-` で
始まればオプションとして食べるので、`cat -` は引数なしと同じ扱い (= 標準入力) になる。

### 7-3 RED → GREEN

RED (直す前の `cmd_file.c`、試験だけ新しい):

```
=== 89 件中 22 件 FAIL ===
EXIT cat_linenum_host=1
```

落ちた 22 件は §6 の新しい場だけ (1〜5 章の 53 件は `ok` のまま)。内訳は
素通し 16 件 (本文 1〜4 番 × 4 刻み。空入力の本文 5 番は 0 バイトどうしで一致
してしまうので `ok`) と `-n` の 6 件。

「FD 0 を閉じない」「引数があるときは FD 0 を読まない」「FD 0 が端末なら
読みに行かない」は RED でも `ok` — 直す前は**そもそも FD 0 を触らない**ので
当然通る。これらは**直しが行き過ぎていないこと**を見る側で、変異
`d_close_stdin` / `e_read_tty` がその目を確かめている。

GREEN (直した後):

```
=== 89 件中 0 件 FAIL ===
EXIT cat_linenum_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_fs_shared.c)
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_file.c)
```

### 7-4 否定側 (`--mutate`) の追加分

| 変異 | 壊すもの | 落ちる件数 |
|---|---|---|
| `c_no_stdin` | 引数が無くても標準入力へ落ちない (欠陥そのもの) | 23 |
| `d_close_stdin` | 読み終わりに `sys_close(0)` する | 4 |
| `e_read_tty` | 端末でも読みに行く (`sys_isatty(0)` の断りを消す) | 3 |

### 7-5 確かめていないこと ([V4])

- **実機 (NP21/W) では回していない。** `echo a | cat` / `cat < file` を
  ゲストで打っていない。贋 FS の FD 0 は実物の `fd_redirect` ではない。
- 端末のときの断り方は `shell_print_help(argv[0])` (`cat` の Usage 行)。
  実際の表示は実機で見ていない。
- `cat` の複数ファイルと標準入力を混ぜる形 (`cat a - b`) は対象外のまま。
