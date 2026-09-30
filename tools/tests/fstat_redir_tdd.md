# fstat_redir TDD 記録 — `fstat` がリダイレクトを見ておらず `isatty` と食い違う

対象票: [`docs/archive/test/TASK_FSTAT_REDIR.md`](../../docs/archive/test/TASK_FSTAT_REDIR.md) の受入 **F4 / F5**
試験: `tools/tests/fstat_redir_host.c` (実物の `fs/vfs.c` + `fs/vfs_fd.c` + `fs/fd_redirect.c` を `#include`)
実行: `python3 -B tools/tests/test_fstat_redir.py [--target] [--mutate]` / `make check-fstat-redir-host`
関連: `tools/tests/test_result_conv.py` (userland の `stat_t` を贋 KAPI で走らせる側)

基点: `feat/gui` = `d213e17`。

## 何を見ているか

| ID | 反例 | 期待 |
|---|---|---|
| F4 | `> file` / `< file` / `2>> file` で `fstat(0/1/2)` | `S_IFREG`。大きさ・時刻・inode・`st_dev` は**実ファイルのもの**で、同じファイルを直に開いた FD の `fstat` と 1 バイトも違わない。解除すれば `S_IFCHR` に戻り、向けていない fd は巻き添えにならない |
| F5 | 状態 (コンソール / ファイル / パイプ) × fd 0/1/2 の**総当たり** | **`fstat` が `S_IFCHR` ⇔ `isatty` が 1**。どちらの API を崩しても落ちる |
| 追加 | パイプ (`FD_TARGET_BUFFER`) の `fstat` | `S_IFIFO`。`st_size` は 0 |
| F6 | `fd>=3` の `fstat` / 引数検査 / コンソールへの `write` | 従来どおり |

リダイレクトは**贋物を置かない** — この票の主題そのものなので `fs/fd_redirect.c` を
実物のまま同じ翻訳単位に取り込む。FS は合成 `VfsOps` で、`stat` が決まった
大きさ (4096) と時刻 (`0x5EED0001` ほか) と inode (1234) を返す。

`[C4]` は静的にも押さえる (`check_single_source()`)。`vfs_isatty` /
`vfs_fstat` / `fd_is_redirected` が 3 つとも `fd_redirect_ifmt()` から引いて
いること、`OS_S_IFIFO` が POSIX と同じ `0x1000` で `S_IFMT` の中で他と
ぶつからないこと。

## パイプ (`FD_TARGET_BUFFER`) をどう答えるか

**`OS_S_IFIFO` を足して (値は POSIX の `S_IFIFO` と同じ `0x1000`)、それを返す。
`st_size` は 0。**

- 調べた結果、`sdk/include/os32/os32_kapi_shared.h` には `S_IFMT` `S_IFCHR`
  `S_IFDIR` `S_IFREG` の 4 つしか無く、**`OS_S_IFIFO` は無かった**。
- `S_IFCHR` のままにはできない。F5 の主張 (「`S_IFCHR` ⇔ `isatty`」) が
  パイプで崩れる — パイプは端末ではないので `isatty` は 0 を返す。
- `S_IFREG` も嘘になる。seek できる実体があるように見えてしまう。
- `0x1000` は `S_IFMT` (`0xF000`) の中で空いていて、POSIX の
  `S_IFIFO` (`010000` = `0x1000`) と同じ値。**推測ではなく POSIX の値**。
- `st_size` は 0。Linux の `fstat` もパイプには 0 を返し、POSIX でも未規定。
  バッファの残量を返すと「これだけ読める」と誤解される。書いた後も 0 の
  ままであることを試験で押さえている。

`userland/cmds/stat.c` の種別表にも `FIFO` を足したが、`stat` はパス名を
取るのでここには来ない (念のため `UNKNOWN` にしないだけ)。

**KAPI の版数は上げていない。** 足したのは `#define` 1 本で、`sdk/kapi.json`
のスロットも `KernelAPI` 構造体も `OS32_Stat` のレイアウトも変えていない
([ABI1] [ABI2] に触れない)。古いバイナリは新しい値を「知らない種別」として
見るだけで、ABI は壊れない。

## 判定の管理元を 1 か所に ([C4])

票 §3-1 の根は「`isatty` と `fstat` が 2 か所で別々に判定していた」こと。
そこで **「fd 0/1/2 はいま何に繋がっているか」に答える関数を 1 本だけ**置いた。

```
fs/fd_redirect.c
    u16 fd_redirect_ifmt(int fd, int *out_file_fd)   <- 唯一の管理元
         FD_TARGET_CONSOLE -> OS_S_IFCHR
         FD_TARGET_FILE    -> OS_S_IFREG  (+ out_file_fd に実ファイルの FD)
         FD_TARGET_BUFFER  -> OS_S_IFIFO
      |
      +- int fd_is_redirected(int fd)   = (ifmt != OS_S_IFCHR)
      |
fs/vfs_fd.c
      +- int vfs_isatty(int fd)         = (ifmt == OS_S_IFCHR)
      +- int vfs_fstat(int fd, ...)     = ifmt が REG なら vfs_fstat(file_fd)、
                                          他は ifmt をそのまま st_mode の種別へ
```

`vfs_fstat` は `OS_S_IFREG` のとき**既存の経路をそのまま通す**
(`vfs_fstat(file_fd, buf)` → `open_files[].ops->stat` → `st_dev` の上書き)。
値を作らないので、`stat <path>` と `fstat 1` が必ず同じ答えになる。

## RED (実装前)

`fs/vfs_fd.c` と `fs/fd_redirect.c` を `d213e17` の内容に戻し
(`fd_redirect_ifmt` は常に `OS_S_IFCHR` を返す仮置き)、ヘッダの
`OS_S_IFIFO` と試験だけを入れた状態。

```
=== 票 TASK_FSTAT_REDIR: fstat がリダイレクトに従い isatty と一致する ===
== F4: リダイレクトの有無で fstat の st_mode が変わる ==
  FAIL > file: fd=1 は S_IFREG (S_IFCHR ではない)
  FAIL > file: キャラクタデバイスとは答えない
  FAIL > file: 大きさは実ファイルのもの
  FAIL > file: 時刻は実ファイルのもの
  FAIL > file: 権限も実ファイルのもの
  FAIL > file: inode も実ファイルのもの
  FAIL > file: 直に開いた FD の fstat と 1 バイトも違わない
  FAIL 向けた fd 1 だけが S_IFREG
  FAIL < file: fd 0 も S_IFREG
  FAIL 2>> file: fd 2 も S_IFREG
== F5: fstat が S_IFCHR ⇔ isatty が 1 (状態 x fd の総当たり) ==
  FAIL > file: fd=1  fstat S_IFCHR=1 <-> isatty=0
  FAIL 0< 1> 2> すべて file: fd=0  fstat S_IFCHR=1 <-> isatty=0
  FAIL 0< 1> 2> すべて file: fd=1  fstat S_IFCHR=1 <-> isatty=0
  FAIL 0< 1> 2> すべて file: fd=2  fstat S_IFCHR=1 <-> isatty=0
  FAIL cmd | cmd (fd 1 がパイプ): fd=1  fstat S_IFCHR=1 <-> isatty=0
  FAIL cmd | cmd (fd 0 がパイプ): fd=0  fstat S_IFCHR=1 <-> isatty=0
== パイプ: S_IFIFO と答える (S_IFCHR でも S_IFREG でもない) ==
  FAIL パイプ: S_IFIFO
  FAIL パイプ: キャラクタデバイスとは答えない
== 回帰: fd>=3 と引数検査は従来どおり ==
  FAIL fd_is_redirected(1)=1 と 非 S_IFCHR が一致

66 件中 19 件 FAIL
EXIT=1
```

`> file: fd=1 fstat S_IFCHR=1 <-> isatty=0` が票 §0 の画面そのもの。

## GREEN (実装後)

```
C4 SINGLE-SOURCE PASS (isatty / fstat / is_redirected はすべて fd_redirect_ifmt() 由来、OS_S_IFIFO=0x1000)
HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c + fs/vfs_fd.c + fs/fd_redirect.c)
66 件中 0 件 FAIL
EXIT fstat_redir_host=0
TARGET i386-elf -Werror COMPILE PASS (fs/vfs_fd.c)
TARGET i386-elf -Werror COMPILE PASS (fs/fd_redirect.c)
```

## 否定側 (`--mutate`)

| 変異 | 崩し方 | 結果 |
|---|---|---|
| `fstat_always_chr` | `vfs_fstat` だけリダイレクトを見ず無条件 `S_IFCHR` (**2026-09-17 の不具合そのもの**) | RED 19 件 (F4 と F5 の両方) |
| `isatty_always_tty` | `vfs_isatty` だけ「fd 0/1/2 は常に端末」に戻す (**片方だけ直した形**) | RED 7 件。**F4 は 1 件も落ちない** — F5 だけが止める |
| `pipe_is_chr` | パイプを `S_IFCHR` と答える | RED 4 件 |
| `file_stat_invented` | 種別だけ合わせて大きさ・時刻・inode は作り話 | RED 4 件 |

2 番目が票 §4 の「F5 が肝」の証明。`isatty_always_tty` は `fstat` 側を
壊さないので **F4 の 24 件はすべて `ok` のまま通る**。「2 つの API が一致する」
という形の主張だけがこれを捕まえる。

## userland 側 (`stat_t`) — 票 §3-2

`userland/tests/stat_t.c` の主張を「特定の値」から「2 つの API の一致」へ
変えた。5 件のままで、内訳は:

1. `stat /bin/sh.bin` が成功する
2. `fstat(1)` が成功する
3. **`fstat(1)` が `S_IFCHR` ⇔ `isatty(1)==1`** (旧: 「fd=1 はキャラクタデバイス」)
4. `stat NONEXIST.TXT` が失敗する
5. **`fstat(0)` が `S_IFCHR` ⇔ `isatty(0)==1`** (旧: 「`isatty(1)==1`」)

5 を fd 0 にしたのは、`cmd | stat_t` では fd 0 がパイプになるから。fd 1 だけ
見ていると同じ穴をもう一度踏む。

これを `tools/tests/test_result_conv.py` の贋 KAPI で 4 場面回す
(`test_result_conv_host.c` に `fk_fstat_ifmt` を足した):

| 場面 | 贋 KAPI | 期待 | 旧 `stat_t` なら |
|---|---|---|---|
| 対話 | `fstat`=CHR / `isatty`=1 | **PASS 5/5** | PASS 5/5 |
| `> file` (受入 F2) | `fstat`=REG / `isatty`=0 | **PASS 5/5** | **FAIL 4/5** ← ランナーが暴いた場面 |
| 食い違い (§1 の不具合) | `fstat`=CHR / `isatty`=0 | **FAIL 3/5** | FAIL 4/5 (気づけない) |
| FS が応えない | `stat`/`fstat` が失敗 | FAIL 1/5 | FAIL 1/5 |

`tools/tests/guest_tests.txt` の `stat_t # PASS 5/5` は据え置き (件数は 5 のまま)。

## 確かめていないこと ([V4])

- **ゲストで動かしていない。** 受入 F1 (`stat_t`) / F2 (`stat_t > file`) /
  F3 (`make check-guest`) は **未実施** — コーダーは配備もエミュレータ操作も
  しない。ホストでは `> file` 相当を贋 KAPI で通してある (上の表) が、
  実物のカーネルで測ったわけではない。
- `make all` / `make check` / `make external` は回していない (個別ターゲットのみ)。
  `fs/vfs_fd.c` と `fs/fd_redirect.c` は `i386-elf-gcc -Werror` で**単体では**
  通ることを確認済み。カーネル全体のリンクと増分サイズは未測定。
- パイプ中の `fstat` を実機で叩いたことはない (`cmd | stat_t` はゲストでのみ
  確かめられる)。ホストでは実物の `fd_redirect.c` を通して見ている。
