CHECK_PAR_ORDER += 188:check-filer-launch-host
check-filer-launch-host:
	python3 -B tools/tests/test_filer_launch.py $(MUT)

.PHONY: check-filer-launch-host
