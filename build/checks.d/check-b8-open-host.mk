CHECK_PAR_ORDER += 074:check-b8-open-host
# 読み取り失敗を「不存在」にしない (票 B8)。実物の ext2 を RAM ディスクへ載せ、
# **間接ブロックを使う大きなディレクトリ**の読み出しを一度だけ落として、
# 実物の vfs_open / vfs_open_sqlite まで通す。O_CREAT が既存ファイルを空に
# しないことを、FD・write_file の呼び出し回数・読み直した中身で押さえる。
# 2 巡目 (Codex 実装レビュー P1-4): 実物の fs/hostdrvfs.c を io.h の差し替えと
# 贋の NP21/W で動かし、OPEN の失敗が NOTFOUND に畳まれないことを見る。
check-b8-open-host:
	python3 -B tools/tests/test_b8_open.py --target
	python3 -B tools/tests/test_b8_hostdrv.py --target

.PHONY: check-b8-open-host
