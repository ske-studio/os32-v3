CHECK_PAR_ORDER += 097:check-fstat-redir-host
# fstat がリダイレクトに従う / isatty と食い違わない (票 TASK_FSTAT_REDIR の F4 F5)。
# 実物の fs/vfs.c + fs/vfs_fd.c + **fs/fd_redirect.c** を #include し、リダイレクトは
# 贋物を置かずに実物を通す。`> file` なら fstat が実体 (S_IFREG・大きさ・時刻・inode)
# を答えること、パイプは S_IFIFO であること、そして **fstat が S_IFCHR ⇔ isatty が 1**
# が状態 (コンソール / ファイル / パイプ) × fd 0/1/2 の総当たりで成り立つことを見る。
# 判定の管理元が 1 か所 ([C4] fd_redirect_ifmt) であることは静的に突き合わせる。
# --mutate は否定側 (fstat だけ見ない版 / **isatty だけ見ない版** /
# パイプを S_IFCHR と答える版 / 実体ではなく作り話を返す版)。
# 記録は tools/tests/fstat_redir_tdd.md。
check-fstat-redir-host:
	python3 -B tools/tests/test_fstat_redir.py --target $(MUT)

.PHONY: check-fstat-redir-host
