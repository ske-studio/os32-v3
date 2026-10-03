CHECK_PAR_ORDER += 005:check-hdd-stage2-host
# HDD インストーラ (票 TASK_HDD_INSTALL 段 2)。cdinst / install の共有部
# (userland/system/inst_disk.c・inst_hdd.c) の判定、実物の cdinst.c と install.c を
# main から (8/17・16/63 の区画表と IPL の幾何、空・再作成・旧配置・未知・2 項目、
# 事前検査の失敗で 1 セクタも書かない、書いた後の失敗は INCOMPLETE)、容量の
# 見積もりと実物の ext2_format_at の像 (dumpe2fs) の一致、boot/ext2_mini.c の
# 508KiB 超を切り詰めない。--mutate は写しの上で変異するので並列で回せる。
# 記録は tools/tests/hdd_stage2_tdd.md。
check-hdd-stage2-host:
	python3 -B tools/tests/test_hdd_stage2.py --target $(MUT)

.PHONY: check-hdd-stage2-host
