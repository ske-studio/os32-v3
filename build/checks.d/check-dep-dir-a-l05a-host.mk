CHECK_PAR_ORDER += 164:check-dep-dir-a-l05a-host
check-dep-dir-a-l05a-host:
	python3 -B tools/tests/test_dep_dir_a_l05a.py $(MUT)

.PHONY: check-dep-dir-a-l05a-host
