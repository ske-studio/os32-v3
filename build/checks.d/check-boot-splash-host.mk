CHECK_PAR_ORDER += 040:check-boot-splash-host
check-boot-splash-host:
	python3 -B tools/tests/test_boot_splash_native.py

.PHONY: check-boot-splash-host
