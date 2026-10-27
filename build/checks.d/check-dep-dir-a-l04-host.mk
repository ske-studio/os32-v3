CHECK_PAR_ORDER += 163:check-dep-dir-a-l04-host
check-dep-dir-a-l04-host:
	python3 -B tools/tests/test_dep_dir_a_l04.py $(MUT)

.PHONY: check-dep-dir-a-l04-host
