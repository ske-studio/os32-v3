CHECK_PAR_ORDER += 162:check-dep-dir-d3-host
check-dep-dir-d3-host:
	python3 -B tools/tests/test_dep_dir_d3.py

.PHONY: check-dep-dir-d3-host
