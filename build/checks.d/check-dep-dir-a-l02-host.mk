CHECK_PAR_ORDER += 161:check-dep-dir-a-l02-host
check-dep-dir-a-l02-host:
	python3 -B tools/tests/test_dep_dir_a_l02.py $(MUT)

.PHONY: check-dep-dir-a-l02-host
