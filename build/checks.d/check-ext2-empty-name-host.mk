CHECK_PAR_ORDER += 075:check-ext2-empty-name-host
# 長さ 0 の名前を ext2 に載せない (票 TASK_EXT2_EMPTY_NAME)。cdinst の NHD の
# ルートに名前の無いディレクトリ (inode 23) が在り、Linux の e2fsck が壊れと
# 判定した — pkg 展開の mkdir("/hd0") が VFS で "/" になり、ext2 が名前 "" で
# 作っていた。実物の ext2 + vfs.c + vfs_fd.c + userland/lib/rt/pkg.c で
# マウント点・末尾 "/"・"//"・"."・".."・256 文字を通し、cdinst と同じ並びで
# 実物の mkpkg.py の PKG を展開した像を**本物の e2fsck -fn** に当てる (clean 必須)。
# --mutants は fs/ の**写し**を変異させるので実物は書き換えない (並列段に置ける)。
# 記録は tools/tests/ext2_empty_name_tdd.md。
check-ext2-empty-name-host:
	python3 -B tools/tests/test_ext2_empty_name.py --target $(MUTS)

.PHONY: check-ext2-empty-name-host
