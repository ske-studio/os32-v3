CHECK_PAR_ORDER += 073:check-vfs-kind-host
# vfs_path_kind のプローブ (票 H1 / 往復 3 の B6)。実物の fs/vfs.c を #include し、
# 「読めなかったディレクトリ」が get_file_size 経由でファイルに化けないことを見る。
check-vfs-kind-host:
	python3 -B tools/tests/test_vfs_kind.py --target

.PHONY: check-vfs-kind-host
