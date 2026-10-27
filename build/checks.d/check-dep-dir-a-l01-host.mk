CHECK_PAR_ORDER += 160:check-dep-dir-a-l01-host
check-dep-dir-a-l01-host:
	python3 -B tools/tests/test_dep_dir_a_l01.py $(MUT)

.PHONY: check-dep-dir-a-l01-host
