CHECK_PAR_ORDER += 104:check-vfs-excl-host
# 排他的作成 O_EXCL (票 H2 §2-1、KAPI v53)。実物の fs/vfs.c + fs/vfs_fd.c を
# #include し、create_excl を**持つ / 持たない**合成 VfsOps で、既存 (ファイル /
# ディレクトリ) が EXIST、判定不能がその負値、O_EXCL 単独が INVAL、非対応 FS が
# NOSYS になることを見る。**非対応の判定が種別検査より先**であることは
# get_file_size / list_dir の呼び出し回数 0 で押さえる。
# --mutate は否定側 (読めなかったを無いと読む版 / 非対応の判定を後ろへ動かした版)。
# 記録は tools/tests/vfs_excl_tdd.md。
check-vfs-excl-host:
	python3 -B tools/tests/test_vfs_excl.py --target $(MUT)

.PHONY: check-vfs-excl-host
