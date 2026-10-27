CHECK_PAR_ORDER += 168:check-dep-dir-a-l09-host
check-dep-dir-a-l09-host:
	python3 -B tools/tests/test_dep_dir_a_l09.py $(MUT)

.PHONY: check-dep-dir-a-l09-host
