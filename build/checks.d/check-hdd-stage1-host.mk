CHECK_PAR_ORDER += 004:check-hdd-stage1-host
# HDD の一時置き場 (票 TASK_HDD_INSTALL 段 1)。区画表の共有部 (drivers/pc98pt.c、
# 標準配置)、ATA の LBA28 / 現在の CHS / 既定の CHS の選択と範囲検査
# (drivers/ide_addr.c)、format の大きさの固定点 (fs/ext2_layout.c)、hdprep の
# 断る条件 (userland/shell/hdprep_plan.c)、ext2_find_partition の失敗と
# ext2_format_at の範囲 (fs/ext2_super.c / ext2_fmt.c、像は e2fsck -fn)、
# Python 側の書き手 (tools/pc98pt.py) と migrate-pt (旧配置の NHD を作って変換
# → e2fsck -fn clean、開始 LBA 不変)。--mutate は写しの上で変異するので並列で回せる。
# 記録は tools/tests/hdd_stage1_tdd.md。
check-hdd-stage1-host:
	python3 -B tools/tests/test_hdd_stage1.py --target $(MUT)

.PHONY: check-hdd-stage1-host
