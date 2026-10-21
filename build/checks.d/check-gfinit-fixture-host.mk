CHECK_PAR_ORDER += 194:check-gfinit-fixture-host
# External transcript required; no ROM, hardware, decoder or GPU execution.
check-gfinit-fixture-host:
	python3 -B tools/tests/test_gfinit_fixture.py $(MUT)

.PHONY: check-gfinit-fixture-host
