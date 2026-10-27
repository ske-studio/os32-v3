CHECK_PAR_ORDER += 168:check-dep-dir-d-splash-host
check-dep-dir-d-splash-host:
	python3 -B tools/tests/test_dep_dir_d_splash.py $(MUT)
.PHONY: check-dep-dir-d-splash-host
