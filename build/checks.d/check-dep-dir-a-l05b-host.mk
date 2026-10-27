CHECK_PAR_ORDER += 165:check-dep-dir-a-l05b-host
check-dep-dir-a-l05b-host:
	python3 -B tools/tests/test_dep_dir_a_l05b.py $(MUT)

.PHONY: check-dep-dir-a-l05b-host
