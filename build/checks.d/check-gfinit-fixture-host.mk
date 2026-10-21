# PM-local only (not in CHECK_PAR_ORDER, so CI never runs it): the nvbios transcript
# (~/os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt, override with GFINIT_SOURCE) is VBIOS-derived
# and stays out of the repo. No ROM, hardware, decoder or GPU execution. Ledger: GF4A-S1-FIXTURE.
check-gfinit-fixture-host:
	python3 -B tools/tests/test_gfinit_fixture.py $(MUT)

.PHONY: check-gfinit-fixture-host
