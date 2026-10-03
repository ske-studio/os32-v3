CHECK_PAR_ORDER += 115:check-nano-inputs-host
# T2f f1a: actual nano archives, providers and build provenance.
check-nano-inputs-host:
	python3 -B tools/tests/test_nano_inputs.py $(MUT)

