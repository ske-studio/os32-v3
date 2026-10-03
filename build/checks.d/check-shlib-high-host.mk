CHECK_PAR_ORDER += 002:check-shlib-high-host
check-shlib-high-host:
	@python3 -B tools/tests/test_shlib_high.py $(MUT)

.PHONY: check-shlib-high-host
