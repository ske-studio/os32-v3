CHECK_PAR_ORDER += 064:check-ring3-guard-host
# exec/ring3_str.c の ring3_guard_active と kernel/gui.c の入口の配線 (2026-09-26、
# filer が窓も出さずに消えた件)。WM はアプリの syscall の中で走るので、
# ring3_in_syscall だけで「アプリ由来」と決めると WM 自身のポインタが KAPI の
# 出力検査で拒否され、アプリが kill される。判定表と gui_call / owner_exit の
# 前後の対をホストで踏み、--mutate は写しの上で変異するので並列 (check-par) で
# 回せる。記録は tools/tests/ring3_guard_tdd.md。
check-ring3-guard-host:
	python3 -B tools/tests/test_ring3_guard.py --target
	python3 -B tools/tests/test_ring3_guard.py $(MUT)

.PHONY: check-ring3-guard-host
