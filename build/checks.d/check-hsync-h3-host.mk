CHECK_PAR_ORDER += 101:check-hsync-h3-host
# hsync の mtime 取得・保存と日時の前置判定 (票 H3、docs/archive/shell/TASK_H3.md
# §6 / §8)。H1 と同じく実物の userland/system/hsync.c を #include し、贋 FS に
# **ノードごとの mtime** と sys_set_mtime (成功 / NOSYS / I/O 失敗) を持たせて回す。
# FILETIME (1601 起点・100ns) -> Unix 秒の境界 (A16) は fs/hostdrv_stat_rules.inc の
# 純関数を直接叩き、vfs_set_mtime の NOSYS 振り分けは実物の fs/vfs.c で見る。
# --mutate は否定側 (日時が不明なのに省略する版などで落ちることの確認)。
# 記録は tools/tests/h3_tdd.md。
check-hsync-h3-host:
	python3 -B tools/tests/test_hsync_h3.py --target $(MUT)

.PHONY: check-hsync-h3-host
