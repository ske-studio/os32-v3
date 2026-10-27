CHECK_PAR_ORDER += 166:check-dep-dir-a-l06-host
check-dep-dir-a-l06-host:
	python3 -B tools/tests/test_dep_dir_a_l06.py $(MUT)

.PHONY: check-dep-dir-a-l06-host
