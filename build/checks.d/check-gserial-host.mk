CHECK_PAR_ORDER += 140:check-gserial-host
# Fake-port framing, random ids and buffered GUI/CUI handoff; no emulator.
check-gserial-host:
	python3 -B tools/tests/test_gserial_host.py $(MUT)

.PHONY: check-gserial-host
