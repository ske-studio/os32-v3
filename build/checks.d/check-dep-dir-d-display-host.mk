CHECK_PAR_ORDER += 167:check-dep-dir-d-display-host
check-dep-dir-d-display-host:
	python3 -B tools/tests/test_dep_dir_d_display.py $(MUT)
.PHONY: check-dep-dir-d-display-host
