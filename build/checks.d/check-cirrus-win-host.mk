CHECK_PAR_ORDER += 127:check-cirrus-win-host
# gfx/backend_cirrus.c の窓の可否 (cirrus_win_usable)。RAM の上端ではなく
# 物理地図 (pgalloc_range_has_ram) で決める — 15MB + 高位 RAM の構成で穴の中の
# バンク窓まで拒んで probe が落ちていた (POLICY_DEBUG §4-34 と同じ形)。
# 実物のソースをホスト ILP32 で回し、i386-elf -Werror でも通す。
# 記録は tools/tests/cirrus_win_tdd.md。
check-cirrus-win-host:
	python3 -B tools/tests/test_cirrus_win.py $(MUT)

