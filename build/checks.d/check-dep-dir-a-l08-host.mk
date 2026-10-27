CHECK_PAR_ORDER += 167:check-dep-dir-a-l08-host
check-dep-dir-a-l08-host:
	python3 -B tools/tests/test_dep_dir_a_l08.py $(MUT)

.PHONY: check-dep-dir-a-l08-host
