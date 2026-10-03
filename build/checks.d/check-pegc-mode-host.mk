CHECK_PAR_ORDER += 128:check-pegc-mode-host
# gfx/backend_pegc.c の 640x480 へ入る / 戻る OUT 列と GDC の FIFO 待ち
# (票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §2 H2・H3・H5、§4)。実物の
# ソースをホスト ILP32 で回し、偽の I/O で ポート・値・順序を期待列と比べる。
# 待ちの上限 (詰まったままでも終わる) も見る。i386-elf -Werror でも通す。
# 記録は tools/tests/pegc_mode_tdd.md。
check-pegc-mode-host:
	python3 -B tools/tests/test_pegc_mode.py $(MUT)

