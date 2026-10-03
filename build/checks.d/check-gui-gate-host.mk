CHECK_PAR_ORDER += 014:check-gui-gate-host
# tools/gui_gate.py の「GUI に入れたか」(R2 の予備調査 2026-09-29、POLICY_DEBUG §4-31)。
# 台本が rshell を閉じずに os32gui を打つと GUI に入らないまま RESULT: OK になっていた。
# 偽のゲスト (rshell の段数・GUI・tvram・/api/status) の上で、ESC → tvram の
# `[Remote shell closed]` の確認と、scrn_ymax == --h かつ grph_disp == 1 の照合を固定する。
# NP21/W は要らない。--mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/gui_gate_tdd.md。
check-gui-gate-host:
	python3 -B tools/tests/test_gui_gate.py $(MUT)

.PHONY: check-gui-gate-host
