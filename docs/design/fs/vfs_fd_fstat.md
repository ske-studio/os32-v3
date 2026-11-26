# sys/fs/vfs_fd.c の fd 0/1/2 の `fstat` / `isatty` — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_FSTAT_REDIR の契約を今のソースと照らして要約
ソース: `sys/fs/vfs_fd.c` (`vfs_fstat`・`vfs_isatty`)・`sys/fs/fd_redirect.c` (`fd_redirect_ifmt`・`fd_is_redirected`)

試験は `fstat_redir_host.c (旧版 80c520c5f4bd)` (実物の `sys/fs/vfs.c`・`sys/fs/vfs_fd.c`・`sys/fs/fd_redirect.c` を取り込む) と、
ゲスト側の `userland/diag/stat_t.c`。

## 1. 判定の管理元は 1 本 ([C4])

「fd 0/1/2 がいま何に繋がっているか」は `fd_redirect_ifmt(fd, &file_fd)` だけが答える。

| リダイレクトの状態 (`redir_table[fd].target_type`) | 戻り値 | `*out_file_fd` |
|---|---|---|
| `FD_TARGET_FILE` (`> file` / `< file` / `2>> file` など) | `OS_S_IFREG` | 向け先の FD |
| `FD_TARGET_BUFFER` (パイプ) | `OS_S_IFIFO` | -1 |
| それ以外 (コンソール)・範囲外の fd | `OS_S_IFCHR` | -1 |

`vfs_isatty`・`vfs_fstat`・`fd_is_redirected` の 3 つとも、これから導く。`isatty` / `fstat` が
`fd_is_redirected` を直に見たり、別々に判定したりしない (2 か所で判定すると食い違う)。
`test_fstat_redir.py (旧版 d5335b145884)` の `check_single_source()` がこの形を静的に見張る。

## 2. 契約

- **不変条件: fd 0/1/2 について `fstat` の種別が `S_IFCHR` ⇔ `isatty` が 1。** コンソール・ファイル・パイプの
  どの状態でも成り立つ。
- `vfs_isatty(fd)` (fd < `VFS_FIRST_FILE_FD`): `fd_redirect_ifmt` が `OS_S_IFCHR` なら 1、それ以外は 0。
- `vfs_fstat(fd)` (fd < `VFS_FIRST_FILE_FD`):
  - ファイルへ向いているなら、向け先の FD で `vfs_fstat` をそのまま返す。大きさ・時刻・inode・`st_dev` は
    実ファイルのもので、同じファイルを直に開いた FD の `fstat` と 1 バイトも違わない。向け先の FD が範囲外なら
    `VFS_ERR_INVAL` (値を作らない)。
  - コンソール (`S_IFCHR`) とパイプ (`S_IFIFO`) は実体を持たない: 構造体を 0 で埋め、`st_mode` = 種別 | 0666、
    `st_ino` = fd + 1、`st_nlink` = 1、`st_dev` = 0、時刻 0、**`st_size` は 0** (パイプに書いた後も 0)。
  - リダイレクトを解除すれば `S_IFCHR` に戻る。向けていない fd は巻き添えにならない。
- fd ≥ 3 の `fstat`・引数の検査 (`buf` が NULL なら `VFS_ERR_INVAL`) は FS に委ねる従来の経路のまま。

## 3. `OS_S_IFIFO`

`sdk/include/os32/os32_kapi_shared.h` の `OS_S_IFIFO` = `0x1000` (POSIX の `S_IFIFO` と同じ値、`OS_S_IFMT` = `0xF000`
の中で他の種別とぶつからない)。パイプを `S_IFCHR` と答えると不変条件が崩れ、`S_IFREG` と答えると seek できる実体が
あるように見えるため。足したのは `#define` 1 本で、KAPI のスロット・`KernelAPI`・`OS32_Stat` の配置は変えていない。

## 4. ゲストの試験の主張 (`stat_t`)

`isatty(1) == 1` のような特定の値ではなく、**「`fstat` が `S_IFCHR` ⇔ `isatty` が 1」の一致**を fd 1 と fd 0
の両方で主張する (`cmd | stat_t` では fd 0 がパイプになる)。対話でもリダイレクトでも成り立つ形。
