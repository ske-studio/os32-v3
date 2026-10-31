# fs/vfs_fd_stdin.inc — stdin の行読取と継続状態

> 状態: **現行 (2026-10-31)**

未リダイレクトの fd=0 を、改行または要求長まで読む。リダイレクトと通常 FD の経路は `vfs_fd.c` に残す。
内部型 `fs/vfs_stdin.h` の `VfsStdinRead` は `{buf, size, done, active}` の16バイト (ILP32)。
`buf` は呼出し元の仮想アドレス、`done` は書込み済みのバイト数で、カーネルの C スタックへの参照を保持しない。

## 開始・再実行・完了

`exec_stdin_state()` が返す AppSlot の状態を使う。対象外の同期読取では同じ型をローカルに置く。
inactive なら引数を保存し done=0 で開始する。active の再発行は保存した buf/size の一致と
`done < size` を確認し、不一致なら active を消して `VFS_ERR_INVAL`。done は符号なし。

`vfs_stdin_continue()` は `input_getchar_kapi()` の取得後、CR を LF に直して
`buf[done]` に書込み、次の文字取得の前に done を増やす。LF または size で累計件数を返す。
入力が負なら既読件数を優先し、未読ならその負値を返す。0長は入力を消費せず0。
通常復帰で active を消す。park の longjmp は active と進捗を保持する。

## 試験と境界

`tools/tests/test_vfs_fd_path.py` の stdin ケースは製品 `vfs_read_fd` に対して、
途中の longjmp、done=1,2 からの再開、2状態の交互読取、CR/LF、NUL、UTF-8 の分割、
余剰入力、0長、負値、引数不一致と完了時の消去を確認する。
exec の復帰は `exec_stdin` の別試験で確認し、
ホストだけでは syscall 全体の受入とはしない。
