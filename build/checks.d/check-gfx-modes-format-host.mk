CHECK_PAR_ORDER += 212:check-gfx-modes-format-host

check-gfx-modes-format-host:
	python3 -B tools/tests/test_gfx_modes_format.py $(MUT)

.PHONY: check-gfx-modes-format-host
