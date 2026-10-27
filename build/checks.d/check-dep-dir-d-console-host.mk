CHECK_PAR_ORDER += 166:check-dep-dir-d-console-host
check-dep-dir-d-console-host:
	python3 -B tools/tests/test_dep_dir_d_console.py $(MUT)
.PHONY: check-dep-dir-d-console-host
