CHECK_PAR_ORDER += 076:check-vfs-fd-path-host
# 票 TASK_VFS_FD_PATH: FD は open 時の inode で読み書きし、unlink・置き換え
# rename・umount で失効する (欠陥 1: 開いた FD の書き込みが同じ名前に作り直した
# ディレクトリを上書きした)。長いパス・深いパスは切り詰めずに断る (欠陥 2)。
# 実物の ext2 + vfs.c + vfs_fd.c + userland/lib/rt/pkg.c で像を e2fsck -fn に当て、
# 実物の mkpkg.py の上限、実物の sdk/crt/syscalls.c の errno (newlib ヘッダ)、
# 実物の kernel/ime_dict.c の開き直し (SQLite 込み) も見る。--mutants は写しを
# 変異させるので実物は書き換えない (並列段に置ける)。
# 記録は tools/tests/vfs_fd_path_tdd.md。
check-vfs-fd-path-host:
	python3 -B tools/tests/test_vfs_fd_path.py --target $(MUTS)

.PHONY: check-vfs-fd-path-host
